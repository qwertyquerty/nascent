if(NOT NASCENT_ICON_NAME)
    set(NASCENT_ICON_NAME "Nascent")
endif()

set(nascent_iconset "${NASCENT_ICON_OUTPUT_DIR}/${NASCENT_ICON_NAME}.iconset")
file(REMOVE_RECURSE "${nascent_iconset}")
file(MAKE_DIRECTORY "${nascent_iconset}")

foreach(nascent_size 16 32 128 256 512)
    math(EXPR nascent_double "${nascent_size} * 2")
    execute_process(COMMAND sips -z ${nascent_size} ${nascent_size} "${NASCENT_ICON_SOURCE}" --out "${nascent_iconset}/icon_${nascent_size}x${nascent_size}.png" OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND sips -z ${nascent_double} ${nascent_double} "${NASCENT_ICON_SOURCE}" --out "${nascent_iconset}/icon_${nascent_size}x${nascent_size}@2x.png" OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
endforeach()

execute_process(COMMAND iconutil -c icns "${nascent_iconset}" -o "${NASCENT_ICON_OUTPUT_DIR}/${NASCENT_ICON_NAME}.icns" COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE_RECURSE "${nascent_iconset}")
