add_Subdirectory(ptgpp)

parsec_addtest_executable(C strange)
target_ptg_sources(strange PRIVATE "strange.jdf")

parsec_addtest_executable(C recursive)
target_ptg_sources(recursive PRIVATE "recursive.jdf")

if(PARSEC_HAVE_RANDOM)
  parsec_addtest_executable(C startup)
  target_ptg_sources(startup PRIVATE "startup.jdf")
endif(PARSEC_HAVE_RANDOM)

parsec_addtest_executable(C complex_deps)
target_ptg_sources(complex_deps PRIVATE "complex_deps.jdf")

add_subdirectory(branching)
add_subdirectory(choice)
add_subdirectory(controlgather)
add_Subdirectory(user-defined-functions)
add_Subdirectory(local-indices)
