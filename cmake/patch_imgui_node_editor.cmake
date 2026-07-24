# Patch for imgui-node-editor: its imgui_extra_math files define
# operator*(float, ImVec2) unconditionally, but ImGui >= 1.92 defines the
# same operator itself (when IMGUI_DEFINE_MATH_OPERATORS is set, which
# node-editor sets) -> duplicate definition, compile error.
# We wrap their definition in the same IMGUI_VERSION_NUM guard the library
# already uses for its other operators. Runs via FetchContent PATCH_COMMAND
# with the working directory = the downloaded source dir.

foreach(f imgui_extra_math.h imgui_extra_math.inl)
    file(READ ${f} content)

    # Idempotency: if the guard is already there, do nothing.
    string(FIND "${content}" "IMGUI_NODE_EDITOR_OPERATOR_PATCH" already)
    if(NOT already EQUAL -1)
        continue()
    endif()

    # Wrap declaration (.h) and definition (.inl). [\r\n] handles CRLF.
    string(REGEX REPLACE
        "inline ImVec2 operator\\*\\(const float lhs, const ImVec2& rhs\\);"
        "# if IMGUI_VERSION_NUM < 19200 // IMGUI_NODE_EDITOR_OPERATOR_PATCH\n\\0\n# endif"
        content "${content}")
    string(REGEX REPLACE
        "inline ImVec2 operator\\*\\(const float lhs, const ImVec2& rhs\\)[\r\n]+{[\r\n]+ *return ImVec2\\(lhs \\* rhs\\.x, lhs \\* rhs\\.y\\);[\r\n]+}"
        "# if IMGUI_VERSION_NUM < 19200 // IMGUI_NODE_EDITOR_OPERATOR_PATCH\n\\0\n# endif"
        content "${content}")

    file(WRITE ${f} "${content}")
endforeach()

message(STATUS "imgui-node-editor: operator* patch applied")
