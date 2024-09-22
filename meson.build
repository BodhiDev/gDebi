project('gdebi',
    version: '0.9.5.8',
    meson_version: '>= 0.47.0'
)

i18n = import('i18n')
py_mod = import('python')
project_id = 'org.debian.gdebi'

message('Looking for dependencies')
py_installation = py_mod.find_installation('python3')
if not py_installation.found()
    error('No valid python3 binary found')
else
    message('Found python3 binary')
endif

subdir('po')
