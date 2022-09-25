find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_gtk REQUIRED gtk+-3.0)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GTK3
        REQUIRED_VARS PC_gtk_FOUND
        )

if (GTK3_FOUND)
    add_library(gtk::gtk INTERFACE IMPORTED)
    target_link_directories(gtk::gtk
            INTERFACE ${PC_gtk_LIBRARY_DIRS}
            )
    target_include_directories(gtk::gtk
            INTERFACE ${PC_gtk_INCLUDE_DIRS}
            )
    target_link_libraries(gtk::gtk
            INTERFACE ${PC_gtk_LIBRARIES}
            )
    target_compile_options(gtk::gtk
            INTERFACE ${PC_gtk_CFLAGS_OTHER}
            )
endif ()
