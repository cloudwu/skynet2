local mainfunc

do
	package.cpath = "build/?.so"
	local fw = require "skynet.framework"

	local function leader()
		local filename = arg[0]
		-- leader
		local threads = {
			fw.thread_create(filename, "worker", 1),
			fw.thread_create(filename, "worker", 2),
			fw.thread_create(filename, "worker", 3),
		}
		fw.thread_join(threads)
	end

	local function worker(id)
		print("worker", id)
	end

	if arg then
		mainfunc = leader
	else
		local duty = ...
		if duty == "worker" then
			mainfunc = worker
		end
	end
end

collectgarbage "collect"	-- collect unused function
mainfunc(select(2,...))
