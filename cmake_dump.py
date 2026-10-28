s = open('CMakeLists.txt', encoding='utf-8').read()
lines = s.splitlines()
for i, l in enumerate(lines[:24], 1):
    print(f'{i}: {l}')
# remove duplicate block
second = '# C1: Release por defecto para evitar -O0 in builds limpia.\n'
old = second + 'if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)\n    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)\n    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")\nendif()\n'
if old in s:
    s = s.replace(old, '', 1)
    open('CMakeLists.txt', 'w', encoding='utf-8', newline='\n').write(s)
    print('REMOVED duplicate')
second_block_old = '''# C1