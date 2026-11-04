# Patch 11 originally used opacity; the dim revision has the same C++ types.
# A symbol-name check would compile calls that make the source page invisible.
function(red_check_cef_content_transform header_text result)
  if(NOT header_text MATCHES "virtual[ \t\r\n]+void[ \t\r\n]+SetContentTransform[ \t\r\n]*\\(")
    set(${result} FALSE PARENT_SCOPE)
    return()
  endif()
  if(NOT header_text MATCHES "SetContentTransform[ \t\r\n]*\\([ \t\r\n]*double[ \t\r\n]+scale[ \t\r\n]*,[ \t\r\n]*double[ \t\r\n]+dim[ \t\r\n]*,[ \t\r\n]*int[ \t\r\n]+duration_ms[ \t\r\n]*\\)")
    message(FATAL_ERROR
      "Incompatible CEF SetContentTransform API: Red requires the dim revision. "
      "The older opacity revision makes Link Peek sources invisible. "
      "Rebuild the engine with current cef-patches (scripts/build_cef.ps1 -SkipUpdate), "
      "then update cmake/cef.lock.json and fetch the matching distribution. "
      "Do not change headers on an existing binary distribution.")
  endif()
  set(${result} TRUE PARENT_SCOPE)
endfunction()

function(red_check_cef_page_container header_text result)
  if(NOT header_text MATCHES "virtual[ \t\r\n]+void[ \t\r\n]+SetContentContainerTransform[ \t\r\n]*\\(")
    set(${result} FALSE PARENT_SCOPE)
    return()
  endif()
  if(NOT header_text MATCHES "SetContentContainerTransform[ \t\r\n]*\\([ \t\r\n]*double[ \t\r\n]+scale[ \t\r\n]*,[ \t\r\n]*double[ \t\r\n]+dim[ \t\r\n]*,[ \t\r\n]*int[ \t\r\n]+duration_ms[ \t\r\n]*,[ \t\r\n]*cef_color_t[ \t\r\n]+veil_color[ \t\r\n]*\\)")
    message(FATAL_ERROR "Incompatible CEF page container API: rebuild against the matching engine and headers")
  endif()
  set(${result} TRUE PARENT_SCOPE)
endfunction()
