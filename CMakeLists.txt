cmake_minimum_required(VERSION 3.20)
project(SIPHERClient VERSION 1.0.0 LANGUAGES CXX)
option(TRUNKMONKEY_BUILD_GUI "Build Qt GUI" ON)
option(TRUNKMONKEY_BUILD_CLI "Build CLI" ON)
option(TRUNKMONKEY_BUILD_TESTS "Build tests" ON)
set(SAK_QT_MAJOR "" CACHE STRING "Force Qt major version for GUI (5 or 6); empty = auto")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# FreeBSD's pkgconf can suppress LOCALBASE -L flags as "system" paths.
# Static PJSIP still needs those directories for optional ports libraries
# such as PortAudio, Opus, bcg729, libupnp, and libuuid. Ask pkgconf to keep
# its reported system library paths; an explicit LOCALBASE link directory is
# added below as a second line of defense.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS} "1")
endif()

# PJSIP is installed as static libraries by the S.I.P.H.E.R. bootstrap.
# For static PJSIP, the public libpjproject list is not enough: codec, audio,
# crypto, UUID, SRTP, WebRTC AEC, and other transitive dependencies must also
# be present on the final link line. FindPkgConfig exposes the exact flags that
# pkg-config returns for --static through the PJPROJECT_STATIC_* variables.
pkg_check_modules(PJPROJECT REQUIRED libpjproject)

if(NOT PJPROJECT_VERSION VERSION_EQUAL "2.17")
 message(FATAL_ERROR "S.I.P.H.E.R. requires PJSIP 2.17; pkg-config reported ${PJPROJECT_VERSION}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
 string(JOIN " " _tm_pj_static_flags ${PJPROJECT_STATIC_LDFLAGS})
 string(FIND " ${_tm_pj_static_flags} " " -lstdc++ " _tm_stdcxx_pos)
 if(NOT _tm_stdcxx_pos EQUAL -1)
  message(FATAL_ERROR
   "FreeBSD PJSIP advertises libstdc++, but TrunkMonkey and packaged Qt use base Clang/libc++. "
   "Rebuild PJSIP with ./build.sh --pjsip using the r11+ builder.")
 endif()
 string(FIND " ${_tm_pj_static_flags} " " -lc++ " _tm_libcxx_pos)
 if(_tm_libcxx_pos EQUAL -1)
  message(FATAL_ERROR
   "FreeBSD PJSIP does not advertise libc++ linkage. Rebuild the managed PJSIP with ./build.sh --pjsip.")
 endif()
 if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "FreeBSD S.I.P.H.E.R. must be compiled with base Clang/libc++; got ${CMAKE_CXX_COMPILER_ID}")
 endif()
endif()

if(NOT PJPROJECT_STATIC_LDFLAGS)
 message(FATAL_ERROR
  "pkg-config did not return static link flags for libpjproject. "
  "Check: pkg-config --cflags --libs --static libpjproject")
endif()

message(STATUS "PJSIP static link libraries: ${PJPROJECT_STATIC_LIBRARIES}")

add_library(trunkmonkey_core
 src/core/Profile.cpp src/core/TextPool.cpp src/core/Logger.cpp src/core/SipTrace.cpp
 src/core/CallSession.cpp src/core/SipAccount.cpp src/core/SipEngine.cpp src/core/MultiCallManager.cpp
 src/core/SipWireMonitor.cpp src/core/CaptureManager.cpp src/core/RuntimePaths.cpp src/core/PbxAudit.cpp)
target_include_directories(trunkmonkey_core PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/include
 ${PJPROJECT_INCLUDE_DIRS})
target_compile_options(trunkmonkey_core PUBLIC ${PJPROJECT_CFLAGS_OTHER})

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  if(NOT TRUNKMONKEY_FREEBSD_LOCALBASE)
    if(DEFINED ENV{LOCALBASE} AND NOT "$ENV{LOCALBASE}" STREQUAL "")
      set(TRUNKMONKEY_FREEBSD_LOCALBASE "$ENV{LOCALBASE}")
    else()
      set(TRUNKMONKEY_FREEBSD_LOCALBASE "/usr/local")
    endif()
  endif()
  message(STATUS "FreeBSD LOCALBASE library path: ${TRUNKMONKEY_FREEBSD_LOCALBASE}/lib")
  target_link_directories(trunkmonkey_core PUBLIC "${TRUNKMONKEY_FREEBSD_LOCALBASE}/lib")
endif()

# Keep pkg-config's --static flags together and in its reported order. This is
# important for PJSIP's static archives and their third-party dependencies.
target_link_libraries(trunkmonkey_core PUBLIC
 ${PJPROJECT_STATIC_LDFLAGS}
 Threads::Threads)
if(MSVC)
 target_compile_options(trunkmonkey_core PRIVATE /W4)
else()
 target_compile_options(trunkmonkey_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
 target_compile_definitions(trunkmonkey_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601 SAK_PORTABLE_BUILD=1)
 target_link_libraries(trunkmonkey_core PUBLIC ws2_32)
endif()
set(TM_INSTALL)

if(TRUNKMONKEY_BUILD_CLI)
 add_executable(trunkmonkey-cli src/cli/main.cpp src/cli/CliDashboard.cpp src/cli/CliDashboard.h)
 target_include_directories(trunkmonkey-cli PRIVATE src/cli)
 target_link_libraries(trunkmonkey-cli PRIVATE trunkmonkey_core)
 set_target_properties(trunkmonkey-cli PROPERTIES OUTPUT_NAME "sipher")
 list(APPEND TM_INSTALL trunkmonkey-cli)
endif()

if(TRUNKMONKEY_BUILD_GUI)
 if(SAK_QT_MAJOR STREQUAL "5")
  find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
  set(SAK_QT_TARGET Qt5)
 elseif(SAK_QT_MAJOR STREQUAL "6")
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  set(SAK_QT_TARGET Qt6)
 else()
  find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
  find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)
  set(SAK_QT_TARGET Qt${QT_VERSION_MAJOR})
 endif()
 add_executable(trunkmonkey-gui src/gui/main.cpp src/gui/MainWindow.cpp src/gui/MainWindow.h src/gui/ProfileDialog.cpp src/gui/ProfileDialog.h)
 set_target_properties(trunkmonkey-gui PROPERTIES AUTOMOC ON)
 if(WIN32)
  set_target_properties(trunkmonkey-gui PROPERTIES WIN32_EXECUTABLE TRUE)
 endif()
 target_include_directories(trunkmonkey-gui PRIVATE src/gui)
 target_link_libraries(trunkmonkey-gui PRIVATE trunkmonkey_core ${SAK_QT_TARGET}::Core ${SAK_QT_TARGET}::Widgets)
 set_target_properties(trunkmonkey-gui PROPERTIES OUTPUT_NAME "sipher-gui")
 list(APPEND TM_INSTALL trunkmonkey-gui)
endif()

if(TRUNKMONKEY_BUILD_TESTS)
 enable_testing()
 add_executable(tm-profile-test tests/profile_test.cpp src/core/Profile.cpp src/core/TextPool.cpp)
 target_include_directories(tm-profile-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
 add_test(NAME profile-test COMMAND tm-profile-test)
 add_executable(tm-sip-trace-test tests/sip_trace_test.cpp src/core/SipTrace.cpp)
 target_include_directories(tm-sip-trace-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
 add_test(NAME sip-trace-test COMMAND tm-sip-trace-test)
 add_executable(tm-runtime-paths-test tests/runtime_paths_test.cpp src/core/RuntimePaths.cpp)
 target_include_directories(tm-runtime-paths-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
 add_test(NAME runtime-paths-test COMMAND tm-runtime-paths-test)
 add_executable(tm-cli-dashboard-test tests/cli_dashboard_test.cpp src/cli/CliDashboard.cpp src/core/Profile.cpp)
 target_include_directories(tm-cli-dashboard-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include src/cli)
 add_test(NAME cli-dashboard-test COMMAND tm-cli-dashboard-test)
 add_executable(tm-capture-manager-test tests/capture_manager_test.cpp src/core/CaptureManager.cpp src/core/Logger.cpp)
 target_include_directories(tm-capture-manager-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
 # CaptureManager launches Wireshark asynchronously with std::thread.  Link the
 # standalone regression target to the platform thread library too (required
 # by FreeBSD/Clang where pthread symbols are not pulled in implicitly).
 target_link_libraries(tm-capture-manager-test PRIVATE Threads::Threads)
 add_test(NAME capture-manager-test COMMAND tm-capture-manager-test)
 add_executable(tm-pbx-audit-test tests/pbx_audit_test.cpp src/core/PbxAudit.cpp src/core/RuntimePaths.cpp)
 target_include_directories(tm-pbx-audit-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
 target_link_libraries(tm-pbx-audit-test PRIVATE Threads::Threads)
 if(WIN32)
  target_link_libraries(tm-pbx-audit-test PRIVATE ws2_32)
 endif()
 add_test(NAME pbx-audit-test COMMAND tm-pbx-audit-test)
set_tests_properties(pbx-audit-test PROPERTIES TIMEOUT 30)
 if(NOT MSVC)
  target_compile_options(tm-profile-test PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tm-sip-trace-test PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tm-runtime-paths-test PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tm-cli-dashboard-test PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tm-capture-manager-test PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tm-pbx-audit-test PRIVATE -Wall -Wextra -Wpedantic)
 endif()
endif()

if(TM_INSTALL)
 install(TARGETS ${TM_INSTALL} RUNTIME DESTINATION bin)
endif()
install(FILES examples/profile.conf.example examples/destinations.txt examples/callerids.txt
        DESTINATION share/trunkmonkey/examples)
install(FILES README.md OPERATOR_GUIDE.md RELEASE_NOTES.md CHANGELOG.md SECURITY_AUDIT.md DESTINATION share/doc/trunkmonkey)
