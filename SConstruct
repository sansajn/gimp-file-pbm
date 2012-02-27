# use scons --debug-build for debug build
AddOption(
	'--debug-build',
	action='store_true',
	dest='debug_build',
	help='debug build',
	default=False)

env = Environment(CCFLAGS='-std=c++0x')
env.ParseConfig('pkg-config --cflags --libs gimp-2.0 gimpui-2.0')

if GetOption('debug_build'):
	env.Append(CCFLAGS=['-g', '-O0', '-DDEBUG'])
else:
	env.Append(CCFLAGS=['-Os'])

env.Program('file-pbm.cpp')

