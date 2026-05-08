deps = [
    'utils.c',
    'jackio.c',
]

progs = {
    'aubiopitch': ['aubiopitch.c'] + deps,
    'aubiomfcc':  ['aubiomfcc.c'] + deps,
    'aubioquiet': ['aubioquiet.c'] + deps,
    'aubiotrack': ['aubiotrack.c'] + deps,
    'aubionotes': ['aubionotes.c'] + deps,
    'aubioonset': ['aubioonset.c'] + deps,
}

if compiler.has_header('getopt.h')
  c_args += ['-DHAVE_GETOPT_H']
endif
if compiler.has_header('unistd.h')
  c_args += ['-DHAVE_UNISTD_H']
endif

foreach name, sources : progs
    executable(name, sources,
            c_args : c_args,
            include_directories : '../src',
            link_with : aubio)
endforeach
