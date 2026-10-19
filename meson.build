project(
    'cyberether-tci-iq-source',
    ['cpp'],
    version: '0.1.0',
    meson_version: '>=1.11.0',
    default_options: [
        'cpp_std=c++20',
        'warning_level=3',
    ],
)

minimum_jetstream_version = '1.15.0'
is_browser = host_machine.system().startswith('emscripten')
cyberether_dep = dependency(
    'jetstream',
    version: '>=' + minimum_jetstream_version,
    required: false,
    fallback: ['cyberether', 'libjetstream_dep'],
    default_options: ['tests=false', 'examples=false', 'remote=disabled'],
)
if not cyberether_dep.found()
    error('CyberEther 1.15.0 or newer development dependency is required.')
endif
if is_browser
    error('The TCI IQ source currently supports native macOS only.')
endif
if host_machine.system() != 'darwin' or host_machine.cpu_family() != 'aarch64'
    error('The initial plugin target is macOS arm64.')
endif

plugin_inc = include_directories('include', 'src')
plugin_bundler = files('tools/package_cep.py')
plugin_example = files('examples/tci-iq-spectrum.yml')
python = import('python').find_installation(required: true)
subdir('src')

plugin_library = static_library(
    'cyberether_tci_iq_source_code',
    plugin_sources,
    include_directories: plugin_inc,
    dependencies: cyberether_dep,
    gnu_symbol_visibility: 'hidden',
    pic: true,
    install: false,
)
plugin_code_dep = declare_dependency(
    include_directories: plugin_inc,
    dependencies: cyberether_dep,
    link_whole: plugin_library,
)
plugin_module = shared_module(
    'cyberether_tci_iq_source',
    [],
    dependencies: plugin_code_dep,
    gnu_symbol_visibility: 'hidden',
    name_prefix: '',
    build_by_default: false,
    install: false,
)

custom_target(
    'cyberether_tci_iq_source_cep',
    input: [plugin_module, plugin_example],
    output: 'cyberether_tci_iq_source.cep',
    command: [
        python,
        plugin_bundler,
        '--output', '@OUTPUT@',
        '--name', meson.project_name(),
        '--version', meson.project_version(),
        '--minimum-jetstream-version', minimum_jetstream_version,
        '--target', 'path=@INPUT0@,system=' + host_machine.system() + ',device=cpu,arch=' + host_machine.cpu_family(),
        '--example', '@INPUT1@',
    ],
    build_by_default: true,
    install: false,
)
