# Embedded Template Library (header-only). etl/ is a git submodule of ETLCPP/etl (MIT),
# pinned to the same release the marcel-cd/etlcpp registry wrapper shipped (20.41.1).
#
# Why not the registry component: its externalproject_add() re-configures ETL's own CMake
# project at build time with an auto-detected host compiler; on machines where esp-clang is
# first on PATH the try-compile hangs forever (observed on ESP-IDF v5.5.5 / Windows).
# ETL needs no build step at all, so this component only exports the include path.
idf_component_register(INCLUDE_DIRS "etl/include")
