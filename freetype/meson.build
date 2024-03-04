dep_freetype = dependency('freetype2')

executable('example1', 'example1.c',
	dependencies : [ dep_freetype, dep_libdrm ],
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
	install : true)

