# SPDX-License-Identifier: Apache-2.0
#
# slice_kpm_monitor: xApp de monitoração KPM por slice (Etapa 2).
#
# O núcleo (skm_core) e os testes não dependem de RIC. O adaptador FlexRIC
# (SKM_BUILD_XAPP=ON) precisa de um FlexRIC já compilado, com as mesmas
# versões de E2AP e KPM do gNB. Dois jeitos de compilar:
#
#   Fora da árvore do FlexRIC (padrão), com a biblioteca e42_xapp_shared e a
#   asn1_nr_rrc (runtime ASN.1):
#     cmake -S . -B build -DFLEXRIC_SRC_DIR=<flexric> -DFLEXRIC_BUILD_DIR=<flexric>/build
#
#   Como subdiretório do FlexRIC (add_subdirectory), com o alvo e42_xapp.
#
# Só o núcleo e os testes: cmake -S . -B build -DSKM_BUILD_XAPP=OFF

cmake_minimum_required(VERSION 3.16)
project(slice_kpm_monitor LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SKM_BUILD_XAPP "Compilar o adaptador FlexRIC" ON)
set(SKM_E2AP_VERSION "E2AP_V3" CACHE STRING "Versão de E2AP do FlexRIC e do gNB")
set(SKM_KPM_VERSION "KPM_V3_00" CACHE STRING "Versão de E2SM-KPM do FlexRIC e do gNB")

add_library(skm_core STATIC src/skm_core.c)
target_include_directories(skm_core PUBLIC src)
target_compile_options(skm_core PRIVATE -Wall -Wextra)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_skm_core tests/test_skm_core.c)
  target_link_libraries(test_skm_core PRIVATE skm_core m)
  add_test(NAME skm_core COMMAND test_skm_core)
endif()

if(NOT SKM_BUILD_XAPP)
  return()
endif()

add_executable(slice_kpm_monitor src/skm_flexric.c)
target_compile_options(slice_kpm_monitor PRIVATE -Wall -Wextra)
target_compile_definitions(slice_kpm_monitor PRIVATE ${SKM_E2AP_VERSION} ${SKM_KPM_VERSION})

if(TARGET e42_xapp)
  # dentro da árvore do FlexRIC (CMAKE_SOURCE_DIR é a raiz dele)
  target_include_directories(slice_kpm_monitor PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(slice_kpm_monitor PRIVATE skm_core e42_xapp -pthread -lsctp -ldl)
else()
  set(FLEXRIC_SRC_DIR "" CACHE PATH "Código-fonte do FlexRIC")
  set(FLEXRIC_BUILD_DIR "" CACHE PATH "Diretório de build do FlexRIC")
  if(NOT EXISTS "${FLEXRIC_SRC_DIR}/src/xApp/e42_xapp_api.h")
    message(FATAL_ERROR "FLEXRIC_SRC_DIR não aponta para o FlexRIC: ${FLEXRIC_SRC_DIR}")
  endif()
  find_library(FLEXRIC_XAPP_LIB e42_xapp_shared PATHS "${FLEXRIC_BUILD_DIR}/src/xApp" NO_DEFAULT_PATH)
  if(NOT FLEXRIC_XAPP_LIB)
    message(FATAL_ERROR "libe42_xapp_shared não encontrada em ${FLEXRIC_BUILD_DIR}/src/xApp")
  endif()
  # A libe42_xapp_shared do FlexRIC não traz o runtime ASN.1 nem as mensagens
  # RRC (asn_OP_*, uper_decode, asn_DEF_NR_UL_DCCH_Message): eles ficam na
  # biblioteca estática asn1_nr_rrc, compilada junto com os exemplos de xApp.
  find_library(FLEXRIC_RRC_LIB asn1_nr_rrc
               PATHS "${FLEXRIC_BUILD_DIR}/examples/xApp/c/monitor/RRC_MESSAGES" NO_DEFAULT_PATH)
  if(NOT FLEXRIC_RRC_LIB)
    message(FATAL_ERROR "libasn1_nr_rrc não encontrada em ${FLEXRIC_BUILD_DIR}/examples/xApp/c/monitor/RRC_MESSAGES")
  endif()
  find_library(SQLITE3_LIB sqlite3)
  target_include_directories(slice_kpm_monitor PRIVATE "${FLEXRIC_SRC_DIR}/src")
  target_link_libraries(slice_kpm_monitor PRIVATE skm_core ${FLEXRIC_XAPP_LIB} ${FLEXRIC_RRC_LIB} -pthread -lsctp -ldl)
  if(SQLITE3_LIB)
    target_link_libraries(slice_kpm_monitor PRIVATE ${SQLITE3_LIB})
  endif()
  get_filename_component(FLEXRIC_XAPP_LIB_DIR "${FLEXRIC_XAPP_LIB}" DIRECTORY)
  set_target_properties(slice_kpm_monitor PROPERTIES BUILD_RPATH "${FLEXRIC_XAPP_LIB_DIR}")
endif()
