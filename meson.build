project('gedl', 'c')

gegldep   = dependency('gegl-0.3')
mrgdep    = dependency('mrg')
sdldep    = dependency('sdl')
gexiv2dep = dependency('gexiv2')

executable('gedl',

   'clip.c',
   'iconographer.c',
   'renderer.c',
   'gedl.c',
   'gedl-ui.c',

   dependencies: [ gegldep, mrgdep, sdldep, gexiv2dep ]
)
