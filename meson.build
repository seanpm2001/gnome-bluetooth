project(
  'gnome-bluetooth', 'c',
  version: '42.alpha',
  license: 'GPL2+',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.53.0',
)

gnomebt_version = meson.project_version()
version_array = gnomebt_version.split('.')
gnomebt_major_version = version_array[0].to_int()

gnomebt_api_version = '2.0'
gnomebt_api_name = '@0@-@1@'.format(meson.project_name(), gnomebt_api_version)

gnomebt_gir_ns = 'GnomeBluetooth'

gnomebt_gettext_package = meson.project_name() + '2'

gnomebt_prefix = get_option('prefix')
gnomebt_datadir = get_option('datadir')
gnomebt_localedir = get_option('localedir')

gnomebt_pkgdatadir = gnomebt_datadir / gnomebt_api_name

# options
enable_gtk_doc = get_option('gtk_doc')
enable_gir = get_option('introspection')

# Before making a release, the GNOMEBT_LT_VERSION string should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
current = 14
revision = 0
age = 1

lt_soversion = current - age
libversion = '@0@.@1@.@2@'.format(lt_soversion, age, revision)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

po_dir = meson.current_source_dir() / 'po'

top_inc = include_directories('.')

cc = meson.get_compiler('c')

config_h = configuration_data()

#i18n
config_h.set_quoted('GETTEXT_PACKAGE', gnomebt_gettext_package)
config_h.set_quoted('LOCALEDIR', gnomebt_prefix / gnomebt_localedir)

# compiler flags
common_flags = [
  '-DHAVE_CONFIG_H',
  '-DBONOBO_DISABLE_DEPRECATED',
  '-DBONOBO_DISABLE_SINGLE_INCLUDES',
  '-DBONOBO_UI_DISABLE_DEPRECATED',
  '-DBONOBO_UI_DISABLE_SINGLE_INCLUDES',
  '-DGCONF_DISABLE_DEPRECATED',
  '-DGCONF_DISABLE_SINGLE_INCLUDES',
  '-DGNOME_DISABLE_DEPRECATED',
  '-DGNOME_DISABLE_SINGLE_INCLUDES',
  '-DGNOME_VFS_DISABLE_DEPRECATED',
  '-DGNOME_VFS_DISABLE_SINGLE_INCLUDES',
  '-DLIBGLADE_DISABLE_DEPRECATED',
  '-DLIBGLADE_DISABLE_SINGLE_INCLUDES',
  '-DLIBSOUP_DISABLE_DEPRECATED',
  '-DLIBSOUP_DISABLE_SINGLE_INCLUDES',
  '-DWNCK_DISABLE_DEPRECATED',
  '-DWNCK_DISABLE_SINGLE_INCLUDES',
]

compiler_flags = []
if get_option('buildtype').contains('debug')
  compiler_flags += cc.get_supported_arguments([
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-prototypes',
    '-Werror=missing-include-dirs',
    '-Werror=pointer-arith',
    '-Werror=return-type',
    '-Wnested-externs',
    '-Wstrict-prototypes',
  ])
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')

gio_dep = dependency('gio-2.0', version: '>= 2.44')
gio_unix_dep = dependency('gio-unix-2.0')
gtk_dep = dependency('gtk4', version: '>= 4.4')
gsound_dep = dependency('gsound')
libadwaita_dep = dependency('libadwaita-1')
libnotify_dep = dependency('libnotify', version: '>= 0.7.0')
libudev_dep = dependency('libudev')

m_dep = cc.find_library('m')

subdir('lib')
if get_option('sendto')
  subdir('sendto')
endif

if enable_gtk_doc
  subdir('docs/reference/libgnome-bluetooth')
endif

subdir('po')
subdir('tests')

configure_file(
  output: 'config.h',
  configuration: config_h,
)

meson.add_install_script(
  'meson_post_install.py',
  get_option('icon_update') ? 'icon_update' : '',
  gnomebt_datadir,
)

summary({'Documentation': enable_gtk_doc,
         'Introspection': enable_gir,
         'Send-to': get_option('sendto'),
         'Run gtk-update-icon-cache': get_option('icon_update')},
        section: 'General')

meson.add_dist_script(
  find_program('check-news.sh').path(),
  '@0@'.format(meson.project_version()),
  'NEWS',
)
