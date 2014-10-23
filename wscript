

from waflib import Configure, Options
Configure.autoconfig = True

def options(opt):
    opt.load("compiler_c gnu_dirs")

def configure(conf):
    conf.load("compiler_c gnu_dirs")

    conf.check_cc(fragment="int main() { return 0; }\n")

    conf.env.CXXFLAGS = ["-O2", "-Wall", "-Werror", "-g"]
    conf.env.CFLAGS = conf.env.CXXFLAGS + ["-std=gnu99", "-D_GNU_SOURCE"]

    conf.check_cc(lib='pthread')

    conf.check(header_name="argconfig/argconfig.h", lib="argconfig",
               uselib_store="ARGCONFIG")

def build(bld):
    bld.program(source="nvram_bench.c",
                target="nvram_bench",
                use="PTHREAD ARGCONFIG")
