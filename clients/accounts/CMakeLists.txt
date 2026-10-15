cutefish_add_client(accounts TARGET AccountsClient
    SOURCES accountsmanager.cpp useraccount.cpp
    HEADERS accountsmanager.h useraccount.h
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
