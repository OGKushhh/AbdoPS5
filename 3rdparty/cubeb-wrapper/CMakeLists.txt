# Wrapper subdirectory for cubeb — exists only to scope the install() override.
#
# In CMake, add_subdirectory() creates a new child scope. Variables and
# function definitions from the child scope DO propagate down to grandchildren
# (cubeb's own CMakeLists), but they do NOT leak back up to siblings. So if we
# define install() as a no-op inside this wrapper, cubeb sees the no-op (good)
# but the parent 3rdparty/CMakeLists.txt scope is unaffected (also good — the
# rest of the project keeps the real install()).

# Pull cubeb options from the parent scope (CACHE variables propagate up
# automatically, but normal variables don't).
# BUILD_TESTS, BUILD_RUST_BUILTIN, etc. are already set as CACHE in the parent.

# Shadow install() with a no-op for cubeb's CMakeLists.txt.
function(install)
	# Silently swallow all install() calls from cubeb's CMakeLists.
	# We don't need cubeb's headers/pkg-config installed into our prefix —
	# we just want to link the static library.
endfunction()

FetchContent_MakeAvailable(cubeb)
