env = Environment()
env.ParseConfig('pkg-config --cflags --libs gimp-2.0 gimpui-2.0')
env.Program('file-pbm.cpp', CPPFLAGS='-std=c++0x -g')
