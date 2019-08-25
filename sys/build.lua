
function EvaluateOptionExpr(expr, options)
	-- No options => Always enabled
	if expr == nil then
		return true
	end

	if type(expr) == 'string' then
		return options[expr]
	end

	if (expr['all-of']) then
		for _, v in ipairs(expr['all-of']) do
			if not EvaluateOptionExpr(v, options) then
				return false
			end
		end

		return true
	end

	if (expr['any-of']) then
		for _, v in ipairs(expr['any-of']) do
			if EvaluateOptionExpr(v, options) then
				return true
			end
		end

		return false
	end
end

function FileSelected(fileEntry, options)

	return EvaluateOptionExpr(fileEntry.options, options)
end

function ListToStr(list)
	if type(list) ~= 'table' then
	    return list
	end

	output = ''
	sep = ''
	for _, str in ipairs(list) do
		output = output .. sep .. str
		sep = ' '
	end

	return output
end

function GetMakeVars(parentConfig)
	cflags = ListToStr(parentConfig.cflags)
	return {
		S = parentConfig.sysdir,
		M = parentConfig.machine,
		SRCDIR = parentConfig.srcdir,
		AWK = "/usr/bin/awk",
		CC = parentConfig.CC,
		CFLAGS = cflags,
		NORMAL_C = parentConfig.CC .. " " .. cflags .. "-c",
		NM = "/usr/bin/nm",
		NMFLAGS = '',

		-- XXX I don't see that these two are set anywhere?
		FEEDER_EQ_PRESETS = "",
		FEEDER_RATE_PRESETS = "",

		-- XXX setable in kernconf?
		KBDMUX_DFLT_KEYMAP = "it.iso",
	}
end

function ProcessBeforeDepend(parentConfig, files, beforedeps, options)

	vars = GetMakeVars(parentConfig)

	for _, f in ipairs(files) do
		print("path: " .. f.path)
		if not f['before-depend'] then
			goto continue
		end

		if not FileSelected(f, options) then
			print(f.path .. ' is not enabled')
			goto continue
		end

		path = f.path

		vars['.TARGET'] = path
		vars['.IMPSRC'] = factory.replace_ext(path, 'o', 'c')

		arglist = factory.shell_split(factory.evaluate_vars(f['compile-with'], vars))
		deplist = factory.flat_list(
			factory.split(factory.evaluate_vars(f['dependency'], vars)),
			"/bin",
			"/lib",
			"/usr/bin",
			"/usr/lib",
			"/usr/local",
			"/usr/local"
		)

		factory.define_command(path, deplist, arglist, { workdir = parentConfig.objdir })

		if f['before-depend'] then
			table.insert(beforedeps, path)
		end

		::continue::
	end
end

function ProcessFiles(parentConfig, files, beforedeps, options)

	vars = GetMakeVars(parentConfig)

	for _, f in ipairs(files) do
		print("path: " .. f.path)
		if not not f['before-depend'] then
			goto continue
		end

		if not FileSelected(f, options) then
			print(f.path .. ' is not enabled')
			goto continue
		end

		if f['no-obj'] then
			target = f.path
			input = factory.replace_ext(path, 'o', 'c')
		else
			target = factory.replace_ext(path, 'c', 'o')
			input = factory.build_path(parentConfig.sysdir, f.path)
		end

		vars['.TARGET'] = target
		vars['.IMPSRC'] = input

		argshell = f['compile-with']
		if not argshell then
			argshell = "${NORMAL_C}"
		end

		arglist = factory.shell_split(factory.evaluate_vars(argshell, vars))
		deplist = factory.flat_list(
			beforedeps,
			factory.split(factory.evaluate_vars(f['dependency'], vars)),
			"/bin",
			"/lib",
			"/usr/bin",
			"/usr/lib"
		)

		factory.define_command(target, deplist, arglist, { workdir = parentConfig.objdir })

		::continue::
	end
end

--[[
function DefineGenassym(parentConf)
	genasym_sh = factory.build_path(parentConf.sysdir, 'kern/genassym.sh'),
	factory.define_command(
		'assym.inc',
		{
			genasym_sh,
			'genassym.o',
			'genoffset_test.o'
	        },
		{ 'env', 'NM=nm', 'NMFLAGS=', 'sh', genasym_sh, '-o', 'assym.inc', 'genassym.o'},
		{ workdir = parentConf.objdir }
	)

	genoffset_sh = factory.build_path(parentConf.sysdir, 'kern/genoffset.sh'),
	factory.define_command(
		'offset.inc',
		{
			genoffset_sh,
			'genoffset.o',
		},
		{ 'env', 'NM=nm', 'NMFLAGS=', 'sh', genoffset_sh, '-o', 'offset.inc', 'genoffset.o' },
		{ workdir = parentConf.objdir }
	)

	--factory.build_path(parentConf.sysdir, parentConf.machine, parentConf.machine,
end
]]--

definitions = {
	{
		name = { "kern-src", "kern-arch-src", "kernconf" },
		process = function(parentConf, kernFiles, archFiles, kernConf)
			options = {}
			makeoptions = {}
			for _, opt in ipairs(kernConf.options) do
				if (type(opt) == 'string') then
					options[opt] = true
				else
					for optname, value in pairs(opt) do
						options[optname] = value
					end
				end
			end
			ident = kernConf.ident
			makeoptions = factory.array_concat(makeoptions, kernConf.makeoptions)

			--DefineGenassym(parentConf)
			beforedeps = {}
			ProcessBeforeDepend(parentConf, kernFiles, options, beforedeps)
			ProcessBeforeDepend(parentConf, archFiles, options, beforedeps)

			ProcessFiles(parentConf, kernFiles, options, beforedeps)
			ProcessFiles(parentConf, archFiles, options, beforedeps)
		end
	}
}

factory.add_definitions(definitions)

srcdir = "/home/rstone/git/freebsd-factory"
sysdir = factory.build_path(srcdir, 'sys')

-- XXX this is massively cut down from the logic in kern.pre.mk
coptflags = {'-O2', '-g', '-pipe', '-fno-strict-aliasing'}
includes = {'-nostdinc', '-I.', '-I' .. sysdir, '-I' .. sysdir .. '/contrib/ck/include' }
defines = {'-D_KERNEL', '-DHAVE_KERNEL_OPTION_HEADERS', '-include', 'opt_global.h'}

topConfig = {
	CC = "/usr/local/bin/clang80",
	cflags = factory.flat_list(coptflags, includes, defines),
	machine = "amd64",
	srcdir = srcdir,
	sysdir = sysdir,
	objdir = "/home/rstone/obj/freebsd-factory/sys",
}

factory.include_config({'sys/conf/files.ucl', 'sys/conf/files.amd64.ucl', 'sys/amd64/conf/GENERIC.ucl'}, topConfig)
