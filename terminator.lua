local fw = require "skynet.framework"

if arg then
	package.cpath = "build/?.so"
	local filename = arg[0]
	-- leader
	local threads = {
		fw.thread_create(filename, "worker", 1),
		fw.thread_create(filename, "worker", 2),
		fw.thread_create(filename, "worker", 3),
	}
	fw.thread_join(threads)
else
	-- thread
	local duty, id = ...
	print("I'm id", id)
end


