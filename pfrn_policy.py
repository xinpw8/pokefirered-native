"""
pfrn_policy.py -- PufferLib 4.0 policy network for pokefirered-native

Follows the NMMO3 policy pattern from pufferlib/ocean/torch.py:
  - encode_observations(obs) -> hidden [B, hidden_dim]
  - decode_actions(hidden) -> (logits, value)
  - LSTM wrapper via pufferlib.models.LSTMWrapper

Observation layout (226 bytes uint8):
  [0:55]    scalar features (player pos, map, party, badges, etc.)
  [55:145]  NPC features (15 NPCs x 6 bytes)
  [145:226] tile grid (9x9 metatile behaviors)

Architecture:
  Tile grid (81 bytes) -> bit-unpack to 8 planes -> Conv2d -> flatten
  Scalars -> normalize + embed categoricals (species, map, direction, weather)
  NPCs -> per-NPC MLP -> masked max-pool
  Concat -> Linear -> LayerNorm -> actor/critic heads
  LSTM via LSTMWrapper for episode memory
"""

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

import pufferlib
import pufferlib.models
from pufferlib.pytorch import layer_init

# Observation layout constants (must match pfrn_env.h)
SCALAR_SIZE = 55
NPC_SIZE_PER = 6
NPC_COUNT = 15
NPC_TOTAL = NPC_SIZE_PER * NPC_COUNT   # 90
TILE_GRID_DIM = 9
TILE_GRID_SIZE = TILE_GRID_DIM * TILE_GRID_DIM  # 81
OBS_SIZE = SCALAR_SIZE + NPC_TOTAL + TILE_GRID_SIZE  # 226

NUM_SPECIES = 413    # FR/LG national dex max + padding
NUM_MAPS = 256       # map_group * 16 + map_num, clamped
NUM_ACTIONS = 8


class PFRNLSTM(pufferlib.models.LSTMWrapper):
    """LSTM wrapper for PFRNPolicy. PufferLib handles the LSTM stepping."""
    def __init__(self, env, policy, input_size=256, hidden_size=256):
        super().__init__(env, policy, input_size, hidden_size)


class PFRNPolicy(nn.Module):
    """
    PufferLib-compatible policy for Pokemon FireRed RL.

    Three observation branches:
      1. Scalar: player pos, map, party, badges, money, weather, step
      2. NPC: 15 nearby NPCs with relative position + attributes
      3. Tile: 9x9 metatile behavior grid bit-unpacked to 8 planes

    Fused into a single hidden vector -> actor + critic heads.
    """

    def __init__(self, env, hidden_size=256, embed_dim=16,
                 tile_channels=32, npc_hidden=32, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.is_continuous = False
        self.num_actions = env.single_action_space.n

        # -- Embeddings for categorical features --
        self.species_embed = nn.Embedding(NUM_SPECIES + 1, embed_dim, padding_idx=0)
        self.map_embed = nn.Embedding(NUM_MAPS, embed_dim)
        self.direction_embed = nn.Embedding(8, 8)
        self.weather_embed = nn.Embedding(16, 8)
        self.npc_graphics_embed = nn.Embedding(256, embed_dim)
        self.npc_direction_embed = nn.Embedding(8, 8)

        # -- Scalar branch --
        # Continuous: player_x, player_y, money, step_counter, hp_pct x 6 = 10
        # Embedded: map(embed_dim) + species x 6(embed_dim*6) + direction(8) + weather(8)
        # Binary: badges(8) + avatar_flags(8) + in_battle(1) + running_oh(3)
        #         + transition_oh(3) + battle_outcome(1) = 24
        scalar_total = 10 + embed_dim + embed_dim * 6 + 8 + 8 + 24
        self.scalar_mlp = nn.Sequential(
            layer_init(nn.Linear(scalar_total, hidden_size)),
            nn.ReLU(),
            layer_init(nn.Linear(hidden_size, hidden_size // 2)),
            nn.ReLU(),
        )

        # -- NPC branch --
        npc_input_dim = 2 + embed_dim + 8 + 1 + 1  # dx, dy, gfx_emb, dir_emb, active, movement
        self.npc_mlp = nn.Sequential(
            layer_init(nn.Linear(npc_input_dim, npc_hidden)),
            nn.ReLU(),
            layer_init(nn.Linear(npc_hidden, npc_hidden)),
            nn.ReLU(),
        )

        # -- Tile branch: 8 bit-planes x 9x9 -> Conv2d --
        self.tile_conv = nn.Sequential(
            layer_init(nn.Conv2d(8, tile_channels, 3, padding=1)),
            nn.ReLU(),
            layer_init(nn.Conv2d(tile_channels, tile_channels, 3, padding=1)),
            nn.ReLU(),
            layer_init(nn.Conv2d(tile_channels, tile_channels, 3, padding=1)),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
        )

        # -- Fusion --
        fusion_input = hidden_size // 2 + npc_hidden + tile_channels
        self.fusion = nn.Sequential(
            layer_init(nn.Linear(fusion_input, hidden_size)),
            nn.ReLU(),
        )

        self.layer_norm = nn.LayerNorm(hidden_size)

        # -- Output heads --
        self.actor = layer_init(nn.Linear(hidden_size, NUM_ACTIONS), std=0.01)
        self.value_fn = layer_init(nn.Linear(hidden_size, 1), std=1)

    def forward(self, x, state=None):
        hidden = self.encode_observations(x)
        actions, value = self.decode_actions(hidden)
        return actions, value

    def forward_train(self, x, state=None):
        return self.forward(x, state)

    def encode_observations(self, obs, state=None):
        """
        Parse raw uint8 obs buffer and encode into feature vector.

        Args:
            obs: [B, 226] uint8 tensor

        Returns:
            features: [B, hidden_size] float tensor
        """
        B = obs.shape[0]
        device = obs.device

        scalar_raw = obs[:, :SCALAR_SIZE]
        npc_raw = obs[:, SCALAR_SIZE:SCALAR_SIZE + NPC_TOTAL]
        tile_raw = obs[:, SCALAR_SIZE + NPC_TOTAL:]

        # ---- Scalar parsing ----

        # Reconstruct int16 little-endian values
        player_x = (scalar_raw[:, 0].to(torch.int32) |
                     (scalar_raw[:, 1].to(torch.int32) << 8))
        # Sign-extend from 16-bit
        player_x = ((player_x + 0x8000) % 0x10000 - 0x8000).float() / 256.0

        player_y = (scalar_raw[:, 2].to(torch.int32) |
                     (scalar_raw[:, 3].to(torch.int32) << 8))
        player_y = ((player_y + 0x8000) % 0x10000 - 0x8000).float() / 256.0

        # Map identity -> embedding
        map_id = (scalar_raw[:, 4].long() * 16 + scalar_raw[:, 5].long()).clamp(0, NUM_MAPS - 1)

        # Player state
        direction = scalar_raw[:, 7].long().clamp(0, 7)
        avatar_flags = scalar_raw[:, 8]
        running_state = scalar_raw[:, 9].long().clamp(0, 2)
        transition_state = scalar_raw[:, 10].long().clamp(0, 2)
        in_battle = scalar_raw[:, 11].float()
        battle_outcome = scalar_raw[:, 12].float().clamp(0, 1)

        # Party: 6 mons x 6 bytes starting at offset 13
        party_species_list = []
        party_hp_list = []
        for i in range(6):
            base = 13 + i * 6
            sp = (scalar_raw[:, base].to(torch.int32) |
                  (scalar_raw[:, base + 1].to(torch.int32) << 8))
            party_species_list.append(sp.long().clamp(0, NUM_SPECIES))
            party_hp_list.append(scalar_raw[:, base + 3].float() / 255.0)

        badges_byte = scalar_raw[:, 49]
        money = (scalar_raw[:, 50].to(torch.int32) |
                 (scalar_raw[:, 51].to(torch.int32) << 8)).float() / 65535.0
        weather = scalar_raw[:, 52].long().clamp(0, 15)
        step_counter = (scalar_raw[:, 53].to(torch.int32) |
                        (scalar_raw[:, 54].to(torch.int32) << 8)).float() / 10000.0

        # Unpack bitmasks
        badges_bits = torch.stack([(badges_byte >> i) & 1 for i in range(8)], dim=1).float()
        flags_bits = torch.stack([(avatar_flags >> i) & 1 for i in range(8)], dim=1).float()
        running_oh = F.one_hot(running_state, 3).float()
        transition_oh = F.one_hot(transition_state, 3).float()

        # Embeddings
        map_emb = self.map_embed(map_id)                          # [B, embed_dim]
        dir_emb = self.direction_embed(direction)                 # [B, 8]
        weather_emb = self.weather_embed(weather)                 # [B, 8]
        species_stack = torch.stack(party_species_list, dim=1)    # [B, 6]
        species_embs = self.species_embed(species_stack).view(B, -1)  # [B, 6*embed_dim]
        hp_stack = torch.stack(party_hp_list, dim=1)              # [B, 6]

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

        scalar_out = self.scalar_mlp(scalar_input)  # [B, hidden_size//2]

        # ---- NPC parsing ----
        npc_reshaped = npc_raw.view(B, NPC_COUNT, NPC_SIZE_PER)
        npc_dx = npc_reshaped[:, :, 0].to(torch.int8).float() / 16.0
        npc_dy = npc_reshaped[:, :, 1].to(torch.int8).float() / 16.0
        npc_gfx_emb = self.npc_graphics_embed(npc_reshaped[:, :, 2].long())
        npc_dir_emb = self.npc_direction_embed(
            npc_reshaped[:, :, 3].long().clamp(0, 7)
        )
        npc_active = npc_reshaped[:, :, 4].float()
        npc_move = npc_reshaped[:, :, 5].float() / 16.0

        npc_input = torch.cat([
            npc_dx.unsqueeze(2), npc_dy.unsqueeze(2),
            npc_gfx_emb, npc_dir_emb,
            npc_active.unsqueeze(2), npc_move.unsqueeze(2),
        ], dim=2)

        npc_features = self.npc_mlp(npc_input) * npc_active.unsqueeze(2)
        npc_pooled = npc_features.max(dim=1)[0]  # [B, npc_hidden]

        # ---- Tile parsing: unpack bytes -> 8 bit-planes -> Conv2d ----
        tile_flat = tile_raw.long()
        bit_planes = torch.stack([(tile_flat >> i) & 1 for i in range(8)], dim=1).float()
        tile_grid = bit_planes.view(B, 8, TILE_GRID_DIM, TILE_GRID_DIM)
        tile_out = self.tile_conv(tile_grid).view(B, -1)  # [B, tile_channels]

        # ---- Fusion ----
        fused = torch.cat([scalar_out, npc_pooled, tile_out], dim=1)
        return self.fusion(fused)  # [B, hidden_size]

    def decode_actions(self, hidden, lookup=None):
        """
        Args:
            hidden: [B, hidden_size] feature vector (post-LSTM if wrapped)

        Returns:
            logits: [B, 10]
            value: [B, 1]
        """
        hidden = self.layer_norm(hidden)
        logits = self.actor(hidden)
        value = self.value_fn(hidden)
        return logits, value
