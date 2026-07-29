show_debug_message("===== DELETE =====");

path_delete(global.test_path);

show_debug_message("exists after delete = " + string(path_exists(global.test_path)));

show_debug_message("===== INVALID PATH =====");

var bad = 123456789;

show_debug_message("exists = " + string(path_exists(bad)));

show_debug_message("===== PATH TEST END =====");

game_end();