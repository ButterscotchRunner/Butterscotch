
 og_array[0]  = "The original"
var neo_array=["FAKE!!!"]
show_debug_message("Original:")
show_debug_message(og_array)
show_debug_message("New array before array_copy:")
show_debug_message(neo_array)
array_copy(neo_array,0,og_array,0,array_length(og_array))
show_debug_message("New array after array_copy:")
show_debug_message(neo_array)