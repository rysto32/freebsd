
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
		SYSDIR = parentConfig.sysdir,
		M = parentConfig.machine,
		SRCDIR = parentConfig.srcdir,
		AWK = "/usr/bin/awk",
		CC = parentConfig.CC,
		ASM_CFLAGS = '-x assembler-with-cpp -DLOCORE ${CFLAGS}', -- XXX ${ASM_CFLAGS.${.IMPSRC:T}}
		CFLAGS = cflags,
		CCACHE_BIN = '',
		WERROR = '', -- XXX
		PROF = '', -- XXX
		NORMAL_C= '${CC} -c ${CFLAGS} ${WERROR} ${PROF} ${.IMPSRC}',
		NORMAL_S= '${CC} -c ${ASM_CFLAGS} ${WERROR} ${.IMPSRC}', -- XXX ${CC:N${CCACHE_BIN}}
		NM = "/usr/bin/nm",
		NMFLAGS = '',

		-- XXX I don't see that these two are set anywhere?
		FEEDER_EQ_PRESETS = "",
		FEEDER_RATE_PRESETS = "",

		-- XXX setable in kernconf?
		KBDMUX_DFLT_KEYMAP = "it.iso",
	}
end

function IsBeforeDepend(fileDef)
	if fileDef['no-implicit-rule'] then
		return true
	end

	local ext = factory.file_ext(fileDef.path)

	return ext == 'm'
end

function ProcessBeforeDepend(parentConfig, files, beforedeps, options)

	local vars = GetMakeVars(parentConfig)
	local f

	for _, f in ipairs(files) do
		--print("path: " .. f.path)
		if not IsBeforeDepend(f) then
			goto continue
		end

		if not FileSelected(f, options) then
			--print(f.path .. ' is not enabled')
			goto continue
		end

		local ret = factory.split(factory.evaluate_vars(f['dependency'], vars))
		local dependency = ret or {}
		local before_depend = f['before-depend']

		print("ret=" .. factory.pretty_print_str(ret))
		print("dep=" .. factory.pretty_print_str(dependency))

		tmpdirs = factory.listify(parentConfig.tmpdir)

		print("Before Depend Path: " .. f.path)
		local arglist
		local compile_with = f['compile-with']
		if compile_with then
			target = f.path

			vars['.TARGET'] = f.path
			vars['.IMPSRC'] = dependency[1]
			arglist = factory.shell_split(factory.evaluate_vars(compile_with, vars))
		else
			local ext = factory.file_ext(f.path)

			if ext == 'm' then
				local awk = factory.build_path(parentConfig.sysdir, 'tools/makeobjops.awk')
				local input = factory.build_path(parentConfig.sysdir, f.path)
				arglist = { vars.AWK, '-f', awk, input, '-h'}
				factory.list_concat(dependency, {awk, input})
				before_depend = true
				target = factory.replace_ext(factory.basename(f.path), 'm', 'h')

				table.insert(tmpdirs, target .. ".tmp")
			else
				print("Unrecognized file extension: " .. f.path)
				os.exit(1)
			end
		end

		local deplist = factory.flat_list(
			dependency,
			"/bin",
			"/lib",
			"/usr/bin",
			"/usr/lib",
			"/usr/local",
			"/usr/share",
			parentConfig.objdir,
			parentConfig.sysdir,
			parentConfig.machineLinks
		)

		local buildopt = { workdir = parentConfig.objdir, tmpdirs = tmpdirs }

		factory.define_command(target, deplist, arglist, buildopt)

		if before_depend then
			factory.list_concat(beforedeps, factory.listify(target))
		end

		::continue::
	end
end

function ProcessFiles(parentConfig, files, beforedeps, options)

	local vars = GetMakeVars(parentConfig)

	for _, f in ipairs(files) do
		--print("path: " .. f.path)
		if IsBeforeDepend(f) then
			goto continue
		end

		if not FileSelected(f, options) then
			--print(f.path .. ' is not enabled')
			goto continue
		end

		local dependency = factory.split(factory.evaluate_vars(f['dependency'], vars)) or {}
		local ext = factory.file_ext(f.path)

		local target
		local input
		if f['no-obj'] and ext ~= 'S' then
			target = f.path
			input = dependency[1]
		else
			if ext == 'S' then
				input = factory.build_path(parentConfig.sysdir, f.path)
				target = factory.basename(factory.replace_ext(f.path, 'S', 'o'))
			elseif ext == 'c' then
				target = factory.basename(factory.replace_ext(f.path, 'c', 'o'))
				input = factory.build_path(parentConfig.sysdir, f.path)
			else
				print("Don't know how to build " .. f.path)
				os.exit(1)
			end
		end

		vars['.TARGET'] = target
		vars['.IMPSRC'] = input
		print(target .. ': .IMPSRC=' .. (input or 'nil'))

		local argshell = f['compile-with']
		if not argshell then
			if ext == 'c' then
				argshell = "${NORMAL_C}"
			elseif ext == 'S' then
				argshell = '${NORMAL_S}'
			else
				print("Don't know how to build " .. f.path)
			end
		end

		tmpdirs = factory.listify(parentConfig.tmpdir)
		table.insert(tmpdirs, factory.build_path(parentConfig.home, '.termcap.db'))
		table.insert(tmpdirs, factory.build_path(parentConfig.home, '.termcap'))

		local arglist = factory.shell_split(factory.evaluate_vars(argshell, vars))
		local deplist = factory.flat_list(
			beforedeps,
			dependency,
			"/bin",
			"/lib",
			"/usr/bin",
			"/usr/lib",
			"/usr/local",
			"/usr/share",
			"opt_global.h",
			parentConfig.objdir,
			parentConfig.sysdir,
			parentConfig.machineLinks,
			'/etc'
		)

		local buildopt = { workdir = parentConfig.objdir, tmpdirs = tmpdirs }

		factory.define_command(target, deplist, arglist, buildopt)

		::continue::
	end
end

function ProcessOptionFile(options, definedOptions, headerSet)
	for _, def in ipairs(options) do
		definedOptions[def.option] = true
		local header
		if def.header then
			header = def.header
		else
			header = 'opt_' .. def.option:lower() .. '.h'
		end
		headerSet[header] = true
	end
end

function ProcessOptionDefs(parentConfig, kernOpt, archOpt, definedOptions)
	local headerSet = {}

	ProcessOptionFile(kernOpt, definedOptions, headerSet)
	ProcessOptionFile(archOpt, definedOptions, headerSet)

	local headers = {'opt_global.h'}
	for file,_ in pairs(headerSet) do
		table.insert(headers, factory.build_path(parentConfig.objdir, file))
	end

	local inputs = { parentConfig.optfile, parentConfig.archoptfile, 'sys/amd64/conf'}
	local arglist = { 'mkoptions', '-o', parentConfig.objdir, '-f', parentConfig.conffile, '-O', parentConfig.optfile, '-O', parentConfig.archoptfile}
	factory.define_command(headers, inputs, arglist, {})
end

function DefineMachineLink(parentConfig, name, source)
	local target = factory.build_path(parentConfig.objdir, name)
	local arglist = {'ln', '-fs', source, target}

	factory.define_command(target, {source}, arglist, {})

	table.insert(parentConfig.machineLinks, target)
end

function AddMachineLinks(parentConfig)
	parentConfig.machineLinks = {}

	DefineMachineLink(parentConfig, 'machine', factory.build_path(parentConfig.sysdir, parentConfig.machine, 'include'))
	if parentConfig.machine == 'i386' or parentConfig.machine == 'amd64' then
		DefineMachineLink(parentConfig, 'x86', factory.build_path(parentConfig.sysdir, 'x86/include'))
	end
end

definitions = {
	{
		name = { "kern-src", 'kern-implicit-src', "kern-arch-src", "kern-options", "kern-arch-options", "kernconf" },
		process = function(parentConf, kernFiles, implicitFiles, archFiles, kernOpt, archOpt, kernConf)
			definedOptions = {}
			ProcessOptionDefs(parentConf, kernOpt, archOpt, definedOptions)
			AddMachineLinks(parentConf)

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
			makeoptions = factory.list_concat(makeoptions, kernConf.makeoptions)

			--DefineGenassym(parentConf)
			beforedeps = {}
			ProcessBeforeDepend(parentConf, kernFiles, options, beforedeps)
			ProcessBeforeDepend(parentConf, implicitFiles, options, beforedeps)
			ProcessBeforeDepend(parentConf, archFiles, options, beforedeps)

			ProcessFiles(parentConf, kernFiles, options, beforedeps)
			ProcessFiles(parentConf, implicitFiles, options, beforedeps)
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
arch_cflags = {'-mno-aes', '-mno-avx', '-mcmodel=kernel', '-mno-red-zone',
	'-mno-mmx', '-mno-sse', '-msoft-float', '-fno-asynchronous-unwind-tables'}

objdir = "/home/rstone/obj/freebsd-factory/sys"

factory.define_command(objdir, {}, {'mkdir', '-p', objdir}, {})

topConfig = {
	CC = "/usr/local/bin/clang80",
	cflags = factory.flat_list(coptflags, includes, defines),
	machine = "amd64",
	srcdir = srcdir,
	sysdir = sysdir,
	objdir = objdir,
	optfile = 'sys/conf/options.ucl',
	archoptfile = 'sys/conf/options.amd64.ucl',
	conffile = 'sys/amd64/conf/GENERIC.ucl',
	tmpdir = '/tmp',
	home = os.getenv('HOME')
}

factory.include_config({'sys/conf/files.ucl', 'sys/conf/files.implicit.ucl', 'sys/conf/files.amd64.ucl', topConfig.optfile, topConfig.archoptfile, topConfig.conffile}, topConfig)
