########################################
# BEGIN_COPYRIGHT
#
# This file is part of SciDB.
# Copyright (C) 2008-2013 SciDB, Inc.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
########################################

# C O M P O N E N T S
#libscidbclient pakcage
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/libscidbclient${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib COMPONENT libscidbclient)

#scidb-utils package
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/iquery" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/gen_matrix" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/benchGen" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/csv2scidb" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/ssdbgen" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidbLoadCsv.sh" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/splitcsv" DESTINATION bin COMPONENT scidb-utils)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/loadcsv.py" DESTINATION bin COMPONENT scidb-utils)
 
#scidb-dev-tools package
install(PROGRAMS "${CMAKE_BINARY_DIR}/tests/unit/unit_tests" DESTINATION bin COMPONENT scidb-dev-tools)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidbtestharness" DESTINATION bin COMPONENT scidb-dev-tools)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/arg_separator" DESTINATION bin COMPONENT scidb-dev-tools)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidbtestprep.py" DESTINATION bin COMPONENT scidb-dev-tools)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/mu_admin.py" DESTINATION bin COMPONENT scidb-dev-tools)
install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/daemon.py" DESTINATION etc COMPONENT scidb-dev-tools)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/mu_config.ini" DESTINATION etc COMPONENT scidb-dev-tools)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/log4j.properties" DESTINATION etc COMPONENT scidb-dev-tools)

#scidb-plugins package
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libpoint${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libmatch${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libbestmatch${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/librational${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libcomplex${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libra_decl${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libmore_math${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libmisc${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libfindstars${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libgroupstars${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libfits${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libmpi${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/libdense_linear_algebra${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
if (TARGET mpi_slave_scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/plugins/mpi_slave_scidb" DESTINATION lib/scidb/plugins COMPONENT scidb-plugins)
endif ()

#scidb package
if (NOT WITHOUT_SERVER)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidb" DESTINATION bin COMPONENT scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidbconf" DESTINATION bin COMPONENT scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidb-prepare-db.sh" DESTINATION bin COMPONENT scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/init-db.sh" DESTINATION bin COMPONENT scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/scidb.py" DESTINATION bin COMPONENT scidb)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/packaging_only/scidb_cores" DESTINATION bin COMPONENT scidb)

    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/data/meta.sql" DESTINATION share/scidb COMPONENT scidb)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/log4cxx.properties" DESTINATION share/scidb COMPONENT scidb)

    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/packaging_only/scidb-sample.conf" DESTINATION etc COMPONENT scidb)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/packaging_only/sample_config.ini" DESTINATION etc COMPONENT scidb)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/packaging_only/config.ini.planet" DESTINATION etc COMPONENT scidb)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/packaging_only/config.ini.ec2" DESTINATION etc COMPONENT scidb)
endif()

if(SWIG_FOUND AND PYTHONLIBS_FOUND AND NOT APPLE)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/_libscidbpython${CMAKE_SHARED_LIBRARY_SUFFIX}" DESTINATION lib COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/libscidbpython.py" DESTINATION lib COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/scidbapi.py" DESTINATION lib COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/README" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/sample.py" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/simplearray.data" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/sample2.py" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/sample2.csv" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/pythonexamples/log4cxx.properties" DESTINATION share/scidb/examples/python COMPONENT libscidbclient-python)
endif(SWIG_FOUND AND PYTHONLIBS_FOUND AND NOT APPLE)

#scidb-dev package
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include COMPONENT scidb-dev PATTERN ".svn" EXCLUDE)

#scidb-doc package
if(SCIDB_DOC_TYPE STREQUAL "FULL" OR SCIDB_DOC_TYPE STREQUAL "API" AND BUILD_USER_DOC)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/doc/api/html/) #create doxygen out dir so make install will work even without 'make doc'
    install(DIRECTORY ${CMAKE_BINARY_DIR}/doc/api/html/ DESTINATION share/doc/api COMPONENT scidb-doc PATTERN ".svn" EXCLUDE)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/doc/user/pdf/ DESTINATION share/doc/user COMPONENT scidb-doc PATTERN ".svn" EXCLUDE)
endif()

# D E B U G   P A C K A G E S
if ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo" AND NOT APPLE)
    #libscidbclient-dbg package
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/libscidbclient${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT libscidbclient-dbg)

    #scidb-utils-dbg package
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/iquery${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/gen_matrix${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/benchGen${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/csv2scidb${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/ssdbgen${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/splitcsv${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-utils-dbg)

    #scidb-dev-tools-dbg package
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/scidbtestharness${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-dev-tools-dbg)
    install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/arg_separator${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-dev-tools-dbg)

    #scidb-dbg package
    if (NOT WITHOUT_SERVER)
        install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/scidb${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-dbg)
        install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/scidbconf${DEBUG_SYMBOLS_EXTENSION}" DESTINATION bin/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-dbg)
    endif()

    #scidb-plugins-dbg package
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libpoint${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libmatch${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libbestmatch${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/librational${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libcomplex${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libra_decl${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libmore_math${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libmisc${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libfits${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libgroupstars${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libfindstars${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libmpi${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    install(FILES "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/libdense_linear_algebra${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT scidb-plugins-dbg)
    if (TARGET mpi_slave_scidb)
        install(PROGRAMS "${GENERAL_OUTPUT_DIRECTORY}/plugins/${DEBUG_SYMBOLS_DIRECTORY}/mpi_slave_scidb${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/scidb/plugins/${DEBUG_SYMBOLS_EXTENSION} COMPONENT scidb-plugins-dbg)
    endif ()

    #libscidbclient-python-dbg package
    if(SWIG_FOUND AND PYTHONLIBS_FOUND AND NOT APPLE)
        install(FILES "${GENERAL_OUTPUT_DIRECTORY}/${DEBUG_SYMBOLS_DIRECTORY}/_libscidbpython${CMAKE_SHARED_LIBRARY_SUFFIX}${DEBUG_SYMBOLS_EXTENSION}" DESTINATION lib/${DEBUG_SYMBOLS_DIRECTORY} COMPONENT libscidbclient-python-dbg)
    endif()
endif ()
# E N D   C O M P O N E N T S

# S O U R C E   P A C K A G E
set(SRC_PACKAGE_FILE_NAME
    "scidb-${SCIDB_VERSION_MAJOR}.${SCIDB_VERSION_MINOR}.${SCIDB_VERSION_PATCH}.${SCIDB_REVISION}")

add_custom_target(src_package
    COMMAND rm -rf ${SRC_PACKAGE_FILE_NAME}
    COMMAND rm -rf ${CMAKE_BINARY_DIR}/${SRC_PACKAGE_FILE_NAME}.tgz
    COMMAND svn export ${CMAKE_SOURCE_DIR} ${SRC_PACKAGE_FILE_NAME}
    COMMAND cp ${CMAKE_BINARY_DIR}/revision ${SRC_PACKAGE_FILE_NAME}
    COMMAND tar -czf ${CMAKE_BINARY_DIR}/${SRC_PACKAGE_FILE_NAME}.tgz ${SRC_PACKAGE_FILE_NAME}
    WORKING_DIRECTORY /tmp
    )
