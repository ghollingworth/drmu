dep_freetype = dependency('freetype2')

executable('example1', 'example1.c', 'runticker.c', 'ticker.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
	install : true,
	dependencies : [
		threads_dep,
		dep_freetype,
		dep_libdrm
	],
)

runticker_drmu = library('runticker_drmu',
	'runticker.c', 'ticker.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
	dependencies : [
		threads_dep,
		dep_freetype,
		dep_libdrm
	],
)

