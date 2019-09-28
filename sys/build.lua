
os_files = {
	'/usr/share/nls',
	'/usr/share/locale',
	'/var/run/ld-elf.so.hints',
	'/etc/libmap.conf',
	'/libexec'
}

function DoEvaluateOptionExpr(expr, options)
	-- No options => Always enabled
	if expr == nil then
		return true
	end

	if type(expr) == 'string' then
		if expr:sub(1, 1) == '!' then
			return not options[expr:sub(2, #expr):lower()]
		else
			return not not options[expr:lower()]
		end
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

function EvaluateOptionExpr(expr, options)
	local ret = DoEvaluateOptionExpr(expr, options)
	--print('Evaluate ' .. factory.pretty_print_str(expr) .. ' = ' .. tostring(ret))
	return ret
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
	cflags = ListToStr(factory.flat_list(parentConfig.cflags, parentConfig.debugPrefixMap))
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
		OBJCOPY = 'objcopy',
		PROF = '', -- XXX
		NORMAL_C= '${CC} -c ${CFLAGS} ${WERROR} ${PROF} ${.IMPSRC}',
		NORMAL_S= '${CC} -c ${ASM_CFLAGS} ${WERROR} ${.IMPSRC}', -- XXX ${CC:N${CCACHE_BIN}}
		NM = "/usr/bin/nm",
		NMFLAGS = '',

		NO_WCONSTANT_CONVERSION=	'-Wno-error-constant-conversion',
		NO_WSHIFT_COUNT_NEGATIVE=	'-Wno-shift-count-negative',
		NO_WSHIFT_COUNT_OVERFLOW=	'-Wno-shift-count-overflow',
		NO_WSELF_ASSIGN=		'-Wno-self-assign',
		NO_WUNNEEDED_INTERNAL_DECL=	'-Wno-error-unneeded-internal-declaration',
		NO_WSOMETIMES_UNINITIALIZED=	'-Wno-error-sometimes-uninitialized',
		NO_WCAST_QUAL=			'-Wno-error-cast-qual',
		NO_WTAUTOLOGICAL_POINTER_COMPARE= '-Wno-tautological-pointer-compare',

		ZLIB_CFLAGS= '-DZ_SOLO',
		ZLIB_C= '${CC} -c ${ZLIB_CFLAGS} ${CFLAGS} ${.IMPSRC}',

		ZSTD_C= '${CC} -c -DZSTD_HEAPMODE=1 -I$S/contrib/zstd/lib/freebsd ${CFLAGS} -I$S/contrib/zstd/lib -I$S/contrib/zstd/lib/common ${WERROR} -Wno-inline -Wno-missing-prototypes ${PROF} -U__BMI__ ${.IMPSRC}',

		-- XXX I don't see that these two are set anywhere?
		FEEDER_EQ_PRESETS = "",
		FEEDER_RATE_PRESETS = "",

		-- XXX setable in kernconf?
		KBDMUX_DFLT_KEYMAP = "it.iso",
	}
end

function HasBeforeDependDefinition(fileDef)
	if fileDef['no-implicit-rule'] then
		return true
	end

	local ext = factory.file_ext(fileDef.path)

	return ext == 'm'
end

function HasNormalDefinition(fileDef)
	return not fileDef['no-implicit-rule']
end

function ProcessRedirect(arglist)
	local i = 1
	local options = {}

	while i <= #arglist do
		if arglist[i] == '<' or arglist[i] == '>' then
			if arglist[i] == '<' then
				options.stdin = arglist[i + 1]
			else
				options.stdout = arglist[i + 1]
			end
			table.remove(arglist, i)
			table.remove(arglist, i)
			goto continue
		end

		i = i + 1
		::continue::
	end

	return options
end

function ProcessBeforeDepend(parentConfig, files, options, lists)

	local vars = GetMakeVars(parentConfig)
	local f

	for _, f in ipairs(files) do
		--print("path: " .. f.path)
		if not HasBeforeDependDefinition(f) then
			goto continue
		end

		if not FileSelected(f, options) then
			--print(f.path .. ' is not enabled')
			goto continue
		end

		local ret = factory.split(factory.evaluate_vars(f['dependency'], vars))
		local dependency = ret or {}
		local before_depend = f['before-depend']

		tmpdirs = factory.listify(parentConfig.tmpdir)

		--print("Before Depend Path: " .. f.path)
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
			parentConfig.objdir,
			factory.build_path(parentConfig.sysdir, sys),
			parentConfig.machineLinks,
			os_files
		)

		local buildopt = ProcessRedirect(arglist)
		buildopt.workdir = parentConfig.objdir
		buildopt.tmpdirs = tmpdirs
		buildopt.statdirs = {'/'}

		factory.define_command(target, deplist, arglist, buildopt)

		if before_depend then
			factory.list_concat(lists.beforedeps, factory.listify(target))
		end

		if not f['no-obj'] then
			table.insert(lists.objs, target)
		end

		::continue::
	end
end

function ProcessFiles(parentConfig, files, options, lists)

	local vars = GetMakeVars(parentConfig)

	for _, f in ipairs(files) do
		--print("path: " .. f.path)
		if not HasNormalDefinition(f) then
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
			elseif ext == 'm' then
				local base = factory.basename(f.path)
				target = factory.replace_ext(base, 'm', 'o')
				input = factory.replace_ext(base, 'm', 'c')
				local mfile = factory.build_path(parentConfig.sysdir, f.path)
				ext = 'c'

				local makeobjops = factory.build_path(parentConfig.sysdir, 'tools/makeobjops.awk')
				local deplist = factory.flat_list(
					mfile,
					makeobjops,
					'/usr/bin',
					'/bin',
					'/lib',
					os_files
				)

				local arglist = {'awk', '-f', makeobjops, mfile, '-c'}
				local buildopts = {workdir = parentConfig.objdir, tmpdirs = input .. '.tmp'}
				factory.define_command(input, deplist, arglist, buildopts)
			elseif ext == 'o' then
				target = f.path
				input = factory.replace_ext(f.path, 'o', 'c')
			else
				print("Don't know how to build " .. f.path)
				os.exit(1)
			end
		end

		vars['.TARGET'] = target
		vars['.IMPSRC'] = input
		--print(target .. ': .IMPSRC=' .. (input or 'nil'))

		local argshell = f['compile-with']
		if not argshell then
			if ext == 'c' or ext == 'o' then
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
			lists.beforedeps,
			dependency,
			input,
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
			'/etc',
			os_files
		)

		local buildopt = ProcessRedirect(arglist)
		buildopt.workdir = parentConfig.objdir
		buildopt.tmpdirs = tmpdirs
		buildopt.statdirs = {"/"}

		factory.define_command(target, deplist, arglist, buildopt)

		if not f['no-obj'] then
			table.insert(lists.objs, target)
		end

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

	local headers = {
		factory.build_path(parentConfig.objdir, 'opt_global.h'),
		factory.build_path(parentConfig.objdir, 'config.c'),
		factory.build_path(parentConfig.objdir, 'env.c'),
		factory.build_path(parentConfig.objdir, 'hints.c'),
	}
	for file,_ in pairs(headerSet) do
		table.insert(headers, factory.build_path(parentConfig.objdir, file))
	end

	local optfile = factory.build_path(parentConfig.srcdir, parentConfig.optfile)
	local archoptfile = factory.build_path(parentConfig.srcdir, parentConfig.archoptfile)
	local conffile = factory.build_path(parentConfig.srcdir, parentConfig.conffile)
	local confdir = factory.build_path(parentConfig.sysdir, parentConfig.machine, 'conf')

	local inputs = factory.flat_list(
		optfile,
		archoptfile,
		confdir,
		'/lib',
		'/usr/lib',
		'/usr/local/lib',
		os_files
	)
	local arglist = { 'mkoptions', '-o', parentConfig.objdir, '-f', conffile, '-O', optfile, '-O', archoptfile}
	factory.define_command(headers, inputs, arglist, { statdirs = "/"})
end

function DefineMachineLink(parentConfig, name, source)
	local target = factory.build_path(parentConfig.objdir, name)
	local arglist = {'ln', '-fs', source, target}

	factory.define_command(target, {source, "/lib", "/bin"}, arglist, {})

	table.insert(parentConfig.machineLinks, target)
	table.insert(parentConfig.debugPrefixMap, '-fdebug-prefix-map=./' .. name .. '=' .. source)
end

function AddMachineLinks(parentConfig)
	parentConfig.machineLinks = {}
	parentConfig.debugPrefixMap = {}

	DefineMachineLink(parentConfig, 'machine', factory.build_path(parentConfig.sysdir, parentConfig.machine, 'include'))
	if parentConfig.machine == 'i386' or parentConfig.machine == 'amd64' then
		DefineMachineLink(parentConfig, 'x86', factory.build_path(parentConfig.sysdir, 'x86/include'))
	end
end

function DefineVers(parentConf, objs)
	local newvers = factory.build_path(parentConf.sysdir, 'conf/newvers.sh')
	-- XXX version!
	local arglist = {
		'env', 'SYSDIR=' .. parentConf.sysdir, "sh", newvers,
		    parentConf.reproFlag, '-I', parentConf.kernIdent, "-C",
		    "clang version 8.0.1 (tags/RELEASE_801/final)"
	}

	local inputs = factory.flat_list(
		'/bin',
		'/usr/bin',
		'/usr/local/bin',
		'/usr/local/libexec',
		'/etc',
		'/usr/share',
		'/lib',
		'/usr/lib',
		'/usr/local/lib',

		parentConf.srcdir,
		newvers,
		os_files,

		-- XXX
		factory.realpath('/home/rstone/repos'),
		'/home/rstone/.gitconfig'
	)

	local o
	local otherObjs = {}
	for _, o in ipairs(objs) do
		if o ~= 'vers.o' then
			table.insert(otherObjs, o)
		end
	end

	local buildopts = {
		workdir = parentConf.objdir,
		tmpdirs = {'/tmp', '/dev/null'},
		order_deps = otherObjs,
		statdirs = { parentConf.objdir }
	}

	factory.define_command({'vers.c', 'version'}, inputs, arglist, buildopts)
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
					options[opt:lower()] = true
				else
					for optname, value in pairs(opt) do
						options[optname:lower()] = value
					end
				end
			end
			ident = kernConf.ident
			makeoptions = factory.list_concat(makeoptions, kernConf.makeoptions)

			lists = {
				beforedeps = {},
				objs = {},
			}
			ProcessBeforeDepend(parentConf, kernFiles, options, lists)
			ProcessBeforeDepend(parentConf, implicitFiles, options, lists)
			ProcessBeforeDepend(parentConf, archFiles, options, lists)

			ProcessFiles(parentConf, kernFiles, options, lists)
			ProcessFiles(parentConf, implicitFiles, options, lists)
			ProcessFiles(parentConf, archFiles, options, lists)

			DefineVers(parentConf, lists.objs)
		end
	}
}

factory.add_definitions(definitions)

srcdir = factory.realpath("/home/rstone/repos/bsd-worktree/factory-build")
sysdir = factory.build_path(srcdir, 'sys')

-- XXX this is massively cut down from the logic in kern.pre.mk
coptflags = {'-O2', '-g', '-pipe', '-fno-strict-aliasing'}
includes = {'-nostdinc', '-I.', '-I' .. sysdir, '-I' .. sysdir .. '/contrib/ck/include' }
defines = {'-D_KERNEL', '-DHAVE_KERNEL_OPTION_HEADERS', '-include', 'opt_global.h'}
arch_cflags = {'-mno-aes', '-mno-avx', '-mcmodel=kernel', '-mno-red-zone',
	'-mno-mmx', '-mno-sse', '-msoft-float', '-fno-asynchronous-unwind-tables'}

warnflags = {'-Wall', '-Wredundant-decls', '-Wnested-externs', '-Wstrict-prototypes',
		'-Wmissing-prototypes', '-Wpointer-arith', '-Wcast-qual',
		'-Wundef', '-Wno-pointer-sign', '-D__printf__=__freebsd_kprintf__',
		'-Wmissing-include-dirs', '-fdiagnostics-show-option',
		'-Wno-unknown-pragmas'}

miscflags = { '-ffreestanding', '-fwrapv', '-fstack-protector', '-gdwarf-2', '-std=iso9899:1999',
		'-fno-omit-frame-pointer', '-mno-omit-leaf-frame-pointer'}

--objdir = "/usr/obj/srcpool/src/rstone/freebsd-factory/amd64.amd64/sys/GENERIC/"
objdir = "/tmp/obj/sys"

factory.define_command(objdir, {}, {'mkdir', '-p', objdir}, {})

kernIdent = "GENERIC"
machine = 'amd64'

topConfig = {
	CC = "/usr/bin/cc",
	cflags = factory.flat_list(coptflags, includes, defines, arch_cflags, warnflags, miscflags),
	machine = "amd64",
	srcdir = srcdir,
	sysdir = sysdir,
	objdir = objdir,
	optfile = 'sys/conf/options.ucl',
	archoptfile = 'sys/conf/options.amd64.ucl',
	kernIdent = kernIdent,
	conffile = 'sys/' .. machine .. '/conf/' .. kernIdent .. '.ucl',
	tmpdir = '/tmp',
	home = os.getenv('HOME'),
	reproFlag = '-R',
}

factory.include_config({'sys/conf/files.ucl', 'sys/conf/files.implicit.ucl', 'sys/conf/files.amd64.ucl', topConfig.optfile, topConfig.archoptfile, topConfig.conffile}, topConfig)
