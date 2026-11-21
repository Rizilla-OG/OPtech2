/* optech2: macros cppcheck can't see without the SDL headers (it doesn't
   read system headers). Force-included into every file it checks. */
#ifndef SDL_VERSION_ATLEAST
#define SDL_VERSION_ATLEAST(X, Y, Z) 1
#endif

/* The out-of-memory check is always followed by the same NULL check in
   plain code (in case Com_Error returns); show cppcheck only that one, or
   every allocation reports a duplicate condition. */
#define YQ2_COM_CHECK_OOM(VAR, ALLOC_FN_NAME, ALLOC_SIZE)
