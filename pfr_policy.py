"""
pfr_policy.py — Policy network for PFRN RL environment

Properly parses the 226-byte observation into typed features:
  - Multi-byte integers reconstructed
  - Categoricals → embeddings
  - Bitmasks → binary vectors
  - 9×9 tile grid → Conv2d spatial processing
  - LSTM for episode memory

Architecture:
  Observation (226 bytes)
    ├── Scalar branch:  reconstructed floats + embeddings → MLP
    ├── NPC branch:     15×6 features → small MLP per NPC → max-pool
    ├── Tile branch:    9×9 metatile behaviors → bit-unpack → Conv2d
    └── Concat → Linear → LSTM → policy head + value head
"""

import torch
import torch.nn as nn
import torch.nn.functional as F

# ── Observation layout constants (must match pfr_env.h) ──

SCALAR_SIZE = 55
NPC_SIZE_PER = 6
NPC_COUNT = 15
NPC_TOTAL = NPC_SIZE_PER * NPC_COUNT  # 90
TILE_GRID_DIM = 9
TILE_GRID_SIZE = TILE_GRID_DIM * TILE_GRID_DIM  # 81
OBS_SIZE = SCALAR_SIZE + NPC_TOTAL + TILE_GRID_SIZE  # 226

# Scalar byte offsets (packed little-endian):
#   player_x: int16 @ 0-1
#   player_y: int16 @ 2-3
#   map_group: u8 @ 4, map_num: u8 @ 5, map_layout_id: u8 @ 6
#   player_direction: u8 @ 7, avatar_flags: u8 @ 8
#   running_state: u8 @ 9, transition_state: u8 @ 10
#   in_battle: u8 @ 11, battle_outcome: u8 @ 12
#   party[6] × {species:u16, level:u8, hp_pct:u8, status:u8, type1:u8} @ 13-48
#   badges: u8 @ 49
#   money: u16 @ 50-51
#   weather: u8 @ 52
#   step_counter: u16 @ 53-54

NUM_SPECIES = 413
NUM_MAPS = 256
NUM_ACTIONS = 8


class PfrPolicy(nn.Module):
    """
    PufferLib-compatible policy for Pokemon FireRed RL.

    Implements:
      - encode_observations(obs) → features [B, hidden_dim]
      - decode_actions(features) → (logits, value)
      - self.lstm for recurrence (PufferLib handles LSTM stepping)
    """

    def __init__(self, env, hidden_dim=256, embed_dim=16, tile_channels=32, npc_hidden=32):
        super().__init__()
        self.hidden_dim = hidden_dim

        # ── Embeddings for categorical features ──
        self.species_embed = nn.Embedding(NUM_SPECIES + 1, embed_dim, padding_idx=0)
        self.map_embed = nn.Embedding(NUM_MAPS, embed_dim)
        self.direction_embed = nn.Embedding(8, 8)
        self.weather_embed = nn.Embedding(16, 8)
        self.npc_graphics_embed = nn.Embedding(256, embed_dim)
        self.npc_direction_embed = nn.Embedding(8, 8)

        # ── Scalar branch ──
        # Continuous: player_x, player_y, money, step_counter, hp_pct×6 = 10
        # Embedded: map(embed_dim) + species×6(embed_dim×6) + direction(8) + weather(8)
        # Binary: badges(8) + flags(8) + in_battle(1) + running_oh(3) + transition_oh(3) + outcome(1) = 24
        scalar_total = 10 + embed_dim + embed_dim * 6 + 8 + 8 + 24
        self.scalar_mlp = nn.Sequential(
            nn.Linear(scalar_total, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim // 2),
            nn.ReLU(),
        )

        # ── NPC branch ──
        npc_input_dim = 2 + embed_dim + 8 + 1 + 1  # dx, dy, gfx_emb, dir_emb, active, movement
        self.npc_mlp = nn.Sequential(
            nn.Linear(npc_input_dim, npc_hidden),
            nn.ReLU(),
            nn.Linear(npc_hidden, npc_hidden),
            nn.ReLU(),
        )

        # ── Tile branch: 8 bit-planes × 9×9 → Conv2d ──
        self.tile_conv = nn.Sequential(
            nn.Conv2d(8, tile_channels, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(tile_channels, tile_channels, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(tile_channels, tile_channels, 3, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
        )

        # ── Fusion ──
        fusion_input = hidden_dim // 2 + npc_hidden + tile_channels
        self.fusion = nn.Sequential(
            nn.Linear(fusion_input, hidden_dim),
            nn.ReLU(),
        )

        # ── LSTM (PufferLib will drive this) ──
        self.lstm = nn.LSTM(hidden_dim, hidden_dim, num_layers=1, batch_first=True)

        # ── Output heads ──
        self.policy_head = nn.Linear(hidden_dim, NUM_ACTIONS)
        self.value_head = nn.Linear(hidden_dim, 1)

    def encode_observations(self, obs, state=None):
        """
        Parse raw uint8 obs buffer and encode into feature vector.

        Args:
            obs: [B, 226] uint8 tensor

        Returns:
            features: [B, hidden_dim] float tensor
        """
        B = obs.shape[0]

        scalar_raw = obs[:, :SCALAR_SIZE]
        npc_raw = obs[:, SCALAR_SIZE:SCALAR_SIZE + NPC_TOTAL]
        tile_raw = obs[:, SCALAR_SIZE + NPC_TOTAL:]

        # ── Scalar parsing ──

        # Reconstruct int16 values (little-endian)
        player_x = ((scalar_raw[:, 1].to(torch.int16) << 8) | scalar_raw[:, 0].to(torch.int16)).float() / 256.0
        player_y = ((scalar_raw[:, 3].to(torch.int16) << 8) | scalar_raw[:, 2].to(torch.int16)).float() / 256.0

        # Map identity → single embedding index
        map_id = (scalar_raw[:, 4].long() * 16 + scalar_raw[:, 5].long()).clamp(0, NUM_MAPS - 1)

        # Player state
        direction = scalar_raw[:, 7].long().clamp(0, 7)
        avatar_flags = scalar_raw[:, 8]
        running_state = scalar_raw[:, 9].long().clamp(0, 2)
        transition_state = scalar_raw[:, 10].long().clamp(0, 2)
        in_battle = scalar_raw[:, 11].float()
        battle_outcome = scalar_raw[:, 12].float().clamp(0, 1)

        # Party: 6 mon × 6 bytes @ offset 13
        party_species_list = []
        party_hp_list = []
        for i in range(6):
            base = 13 + i * 6
            sp = (scalar_raw[:, base + 1].long() << 8) | scalar_raw[:, base].long()
            party_species_list.append(sp.clamp(0, NUM_SPECIES))
            party_hp_list.append(scalar_raw[:, base + 3].float() / 255.0)

        badges_byte = scalar_raw[:, 49]
        money = ((scalar_raw[:, 51].long() << 8) | scalar_raw[:, 50].long()).float() / 65535.0
        weather = scalar_raw[:, 52].long().clamp(0, 15)
        step_counter = ((scalar_raw[:, 54].long() << 8) | scalar_raw[:, 53].long()).float() / 10000.0

        # Unpack bitmasks
        badges_bits = torch.stack([(badges_byte >> i) & 1 for i in range(8)], dim=1).float()
        flags_bits = torch.stack([(avatar_flags >> i) & 1 for i in range(8)], dim=1).float()
        running_oh = F.one_hot(running_state, 3).float()
        transition_oh = F.one_hot(transition_state, 3).float()

        # Embeddings
        map_emb = self.map_embed(map_id)                    # [B, embed_dim]
        dir_emb = self.direction_embed(direction)           # [B, 8]
        weather_emb = self.weather_embed(weather)           # [B, 8]
        species_stack = torch.stack(party_species_list, dim=1)
        species_embs = self.species_embed(species_stack).view(B, -1)  # [B, 6*embed_dim]
        hp_stack = torch.stack(party_hp_list, dim=1)        # [B, 6]

        # Continuous features
        scalar_cont = torch.cat([
            player_x.unsqueeze(1), player_y.unsqueeze(1),
            money.unsqueeze(1), step_counter.unsqueeze(1),
            hp_stack,
        ], dim=1)  # [B, 10]

        # Full scalar input
        scalar_input = torch.cat([
            scalar_cont, map_emb, species_embs, dir_emb, weather_emb,
            badges_bits, flags_bits,
            in_battle.unsqueeze(1), running_oh, transition_oh,
            battle_outcome.unsqueeze(1),
        ], dim=1)

        scalar_out = self.scalar_mlp(scalar_input)  # [B, hidden//2]

        # ── NPC parsing ──
        npc_reshaped = npc_raw.view(B, NPC_COUNT, NPC_SIZE_PER)
        npc_dx = npc_reshaped[:, :, 0].to(torch.int8).float() / 16.0
        npc_dy = npc_reshaped[:, :, 1].to(torch.int8).float() / 16.0
        npc_gfx_emb = self.npc_graphics_embed(npc_reshaped[:, :, 2].long())
        npc_dir_emb = self.npc_direction_embed(npc_reshaped[:, :, 3].long().clamp(0, 7))
        npc_active = npc_reshaped[:, :, 4].float()
        npc_move = npc_reshaped[:, :, 5].float() / 16.0

        npc_input = torch.cat([
            npc_dx.unsqueeze(2), npc_dy.unsqueeze(2),
            npc_gfx_emb, npc_dir_emb,
            npc_active.unsqueeze(2), npc_move.unsqueeze(2),
        ], dim=2)

        npc_features = self.npc_mlp(npc_input) * npc_active.unsqueeze(2)
        npc_pooled = npc_features.max(dim=1)[0]  # [B, npc_hidden]

        # ── Tile parsing: unpack bytes → 8 bit-planes → Conv2d ──
        tile_flat = tile_raw.long()
        bit_planes = torch.stack([(tile_flat >> i) & 1 for i in range(8)], dim=1).float()
        tile_grid = bit_planes.view(B, 8, TILE_GRID_DIM, TILE_GRID_DIM)
        tile_out = self.tile_conv(tile_grid).view(B, -1)  # [B, tile_channels]

        # ── Fusion ──
        fused = torch.cat([scalar_out, npc_pooled, tile_out], dim=1)
        return self.fusion(fused)  # [B, hidden_dim]

    def decode_actions(self, hidden, lookup=None):
        """
        Args:
            hidden: [B, hidden_dim] feature vector (post-LSTM)

        Returns:
            logits: [B, 10]
            value: [B, 1]
        """
        logits = self.policy_head(hidden)
        value = self.value_head(hidden)
        return logits, value
