
local host = require("./host_api")

local skeleton: number? = nil
local elapsed = 0
local next_change = 0

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

			local index = (math.floor(elapsed / EVERY) % #CYCLE) + 1
			if host.spine_play(who, CYCLE[index], true, 0) then
				next_change = elapsed + EVERY
			end

			if index == 3 then
				host.spine_queue(who, "shoot", false, 0, 1)
			end
		end,
	},
}
