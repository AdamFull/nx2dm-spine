--!strict
-- Driving a Spine skeleton from script.
--
-- Everything here goes through the spine_* services the module exposes. The
-- engine knows nothing about them: they arrive because project.json asks for
-- the spine module, and the declarations host.luau checks against are written
-- into host_modules.luau by the build.

local host = require("./host")

local skeleton: number? = nil
local elapsed = 0
local next_change = 0

-- Spineboy's own animations, in the order they read well.
local CYCLE = { "walk", "run", "idle" }
local EVERY = 3

return {
	["game.skeleton"] = {
		run = function(ctx: { dt: number })
			if skeleton == nil then
				skeleton = host.find("skeleton")
				if skeleton == nil then
					return
				end
			end
			local who = skeleton :: number

			elapsed += ctx.dt
			if elapsed < next_change then
				return
			end

			-- Track 0 is the body. spine_play answers false for a name the
			-- skeleton does not carry, so a typo is visible rather than silent.
			local index = (math.floor(elapsed / EVERY) % #CYCLE) + 1
			if host.spine_play(who, CYCLE[index], true, 0) then
				next_change = elapsed + EVERY
			end

			-- And a one-shot over the top, queued on track 1 so the walk keeps
			-- running underneath it.
			if index == 3 then
				host.spine_queue(who, "shoot", false, 0, 1)
			end
		end,
	},
}
