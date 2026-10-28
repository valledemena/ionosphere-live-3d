import re
p='CMakeLists.txt'
s=open(p,encoding='utf-8').read()
s=s.replace('''# C1: Release por defecto para evitar -O0 in builds limpia.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
''','')
open(p,'w',encoding='utf-8',newline='\n').write(s)
print('OK')