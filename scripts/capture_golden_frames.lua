-- capture_golden_frames.lua — mGBA Lua script for golden frame capture
--
-- Boots pokefirered.gba with no save data, replays input from the scripted
-- input file, and captures milestone frames as PNG screenshots.
--
-- Usage (via wrapper script):
--   ./scripts/capture_golden_frames.sh [mgba_path] [rom_path]
--
-- Or directly:
--   mgba -s scripts/capture_golden_frames.lua path/to/pokefirered.gba

-- Resolve paths relative to the script location
local SCRIPT_DIR = script.dir
local PROJECT_DIR = SCRIPT_DIR .. "/.."
local MANIFEST_PATH = PROJECT_DIR .. "/golden_frames/manifest.txt"
local OUTPUT_DIR = PROJECT_DIR .. "/golden_frames"
local DEFAULT_INPUT = PROJECT_DIR .. "/tests/pfr_play_oak_path.input"

-- GBA button constants (match GBA_KEY enum values)
local KEY_MAP = {
    A      = 0,
    B      = 1,
    SELECT = 2,
    START  = 3,
    RIGHT  = 4,
    LEFT   = 5,
    UP     = 6,
    DOWN   = 7,
    R      = 8,
    L      = 9,
}

-- Parse the scripted input file (format: start_frame end_frame BUTTON [BUTTON...])
local function parse_input_file(path)
    local ranges = {}
    local f = io.open(path, "r")
    if not f then
        console:error("Cannot open input file: " .. path)
        return ranges
    end
    for line in f:lines() do
        local trimmed = line:match("^%s*(.-)%s*$")
        if trimmed ~= "" and trimmed:sub(1, 1) ~= "#" then
            local parts = {}
            for token in trimmed:gmatch("%S+") do
                parts[#parts + 1] = token
            end
            if #parts >= 3 then
                local start_frame = tonumber(parts[1])
                local end_frame = tonumber(parts[2])
                -- Build a bitmask for all buttons in this range
                local mask = 0
                for i = 3, #parts do
                    local name = parts[i]:upper()
                    if KEY_MAP[name] ~= nil then
                        mask = mask | (1 << KEY_MAP[name])
                    end
                end
                if start_frame and end_frame and mask ~= 0 then
                    ranges[#ranges + 1] = {
                        start_frame = start_frame,
                        end_frame = end_frame,
                        mask = mask,
                    }
                end
            end
        end
    end
    f:close()
    return ranges
end

-- Parse the manifest file
local function parse_manifest(path)
    local milestones = {}
    local input_file = nil
    local f = io.open(path, "r")
    if not f then
        console:error("Cannot open manifest: " .. path)
        return milestones, input_file
    end
    for line in f:lines() do
        local trimmed = line:match("^%s*(.-)%s*$")
        if trimmed ~= "" and trimmed:sub(1, 1) ~= "#" then
            local parts = {}
            for token in trimmed:gmatch("%S+") do
                parts[#parts + 1] = token
            end
            if #parts >= 3 then
                local name = parts[1]
                local frame = tonumber(parts[2])
                local infile = parts[3]
                if frame then
                    milestones[#milestones + 1] = {
                        name = name,
                        frame = frame,
                    }
                    if not input_file then
                        input_file = infile
                    end
                end
            end
        end
    end
    f:close()
    table.sort(milestones, function(a, b) return a.frame < b.frame end)
    return milestones, input_file
end

-- Build a lookup table: frame_number -> combined button bitmask
local function build_frame_keys(input_ranges, max_frame)
    local keys = {}
    for _, range in ipairs(input_ranges) do
        for f = range.start_frame, math.min(range.end_frame, max_frame) do
            keys[f] = (keys[f] or 0) | range.mask
        end
    end
    return keys
end

-- Build a set: frame_number -> milestone info
local function build_milestone_set(milestones)
    local set = {}
    for _, ms in ipairs(milestones) do
        set[ms.frame] = ms
    end
    return set
end

-- Load manifest and input
local milestones, input_rel_path = parse_manifest(MANIFEST_PATH)
if #milestones == 0 then
    console:error("No milestones found in manifest")
    return
end

local input_path
if input_rel_path then
    input_path = PROJECT_DIR .. "/" .. input_rel_path
else
    input_path = DEFAULT_INPUT
end
local input_ranges = parse_input_file(input_path)

local last_frame = milestones[#milestones].frame + 10
local frame_keys = build_frame_keys(input_ranges, last_frame)
local milestone_set = build_milestone_set(milestones)

console:log(string.format("Golden capture: %d milestones, %d input ranges, running to frame %d",
    #milestones, #input_ranges, last_frame))

local captured = 0
local frame_count = 0

-- Use keysRead callback for input injection (called each frame before input is processed)
callbacks:add("keysRead", function()
    frame_count = frame_count + 1
    local mask = frame_keys[frame_count]
    if mask then
        emu:addKeys(mask)
    end
end)

-- Use frame callback for screenshots (called after each frame is rendered)
callbacks:add("frame", function()
    local ms = milestone_set[frame_count]
    if ms then
        local out_path = OUTPUT_DIR .. "/" .. ms.name .. ".png"
        emu:screenshot(out_path)
        captured = captured + 1
        console:log(string.format("[%d/%d] Captured '%s' at frame %d",
            captured, #milestones, ms.name, frame_count))
    end

    -- After all milestones captured + buffer, signal completion
    if frame_count >= last_frame then
        console:log(string.format("Done: %d/%d milestones captured", captured, #milestones))
        -- Write a completion marker file so the wrapper script knows we're done
        local marker = io.open(OUTPUT_DIR .. "/.capture_done", "w")
        if marker then
            marker:write(string.format("%d\n", captured))
            marker:close()
        end
    end
end)

-- Reset to ensure clean boot state
emu:reset()
