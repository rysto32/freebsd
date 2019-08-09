
function ProcessBeforeDeps(parentConf, files, beforedeps)
	for _, f in ipairs(files['kern']) do
		if ! f['before-depend'] then
			continue
		end

		vars = {
			S = parentConfig.sysdir,
			SRCDIR = parentConfig.srcdir,
			AWK = "/usr/bin/awk",
		}
		command = factory
	end
end

definitions = {
	{
		name = [ "kern-src", "kern-arch-src", "kernconf" ],
		process = function(parentConf, kernFiles, archFiles, kernConfList)
			options = {}
			makeoptions = {}
			for _, kernConf in ipairs(kernConfList.kernconf) do
				for _, opt in ipairs(kernConf.options) do
					options[opt] = true
				end
				ident = kernConf.ident
				makeoptions = table.array_concat(makeoptions, kernConf.makeoptions)
			end

			beforedeps = {}
			ProcessBeforeDeps(kernFiles['kern-src'], beforedeps)
			ProcessBeforeDeps(archFiles['kern-arch-src'], beforedeps)
		end
	}
}

factory.add_definitions(definitions)
