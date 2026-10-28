import re
p='CMakeLists.txt'
s=open(p,encoding='utf-8').read()
old='# C1: Release por defecto para evitar -O0 in builds limpia.\nif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)\n    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)\n    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")\nendif()\n'
assert old in s
s=s.replace(old,'',1)
open(p,'w',encoding='utf-8',newline='\n').write(s)
print('OK')