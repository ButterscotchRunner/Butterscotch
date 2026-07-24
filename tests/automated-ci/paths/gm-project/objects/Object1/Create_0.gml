show_debug_message("===== PATH TEST BEGIN =====");

function dump_path(p)
{
    show_debug_message("-------------------------");
    show_debug_message("exists      = " + string(path_exists(p)));

    if (!path_exists(p))
        return;

    show_debug_message("length      = " + string(path_get_length(p)));
    show_debug_message("points      = " + string(path_get_number(p)));
    show_debug_message("closed      = " + string(path_get_closed(p)));
    show_debug_message("kind        = " + string(path_get_kind(p)));
    show_debug_message("precision   = " + string(path_get_precision(p)));

    var n = path_get_number(p);

    for (var i = 0; i < n; i++)
    {
        show_debug_message(
            string(i)
            + ": x=" + string(path_get_point_x(p, i))
            + " y=" + string(path_get_point_y(p, i))
            + " speed=" + string(path_get_point_speed(p, i))
        );
    }

    // Sample positions along the path
    var samples = [0, 0.25, 0.5, 0.75, 1.0];

    for (var i = 0; i < array_length(samples); i++)
    {
        var pos = samples[i];

        show_debug_message(
            "t=" + string(pos)
            + " -> x=" + string(path_get_x(p, pos))
            + " y=" + string(path_get_y(p, pos))
            + " speed=" + string(path_get_speed(p, pos))
        );
    }
}

// Create path

var p = path_add();

show_debug_message("Created path id = " + string(p));

path_add_point(p, 0,   0,   100);
path_add_point(p, 100, 0,   200);
path_add_point(p, 100, 100, 300);
path_add_point(p, 0,   100, 400);

dump_path(p);

// Precision

show_debug_message("===== CHANGE PRECISION =====");

path_set_precision(p, 8);
dump_path(p);

// Kind

show_debug_message("===== CHANGE KIND =====");

path_set_kind(p, 1);
dump_path(p);

// Closed

show_debug_message("===== MAKE CLOSED =====");

path_set_closed(p, true);
dump_path(p);

// Clear

show_debug_message("===== CLEAR POINTS =====");

path_clear_points(p);

dump_path(p);

// Add new points

show_debug_message("===== ADD NEW POINTS =====");

path_add_point(p, 50, 50, 10);
path_add_point(p, 150, 50, 20);

dump_path(p);

// Delete

show_debug_message("===== DELETE =====");

path_delete(p);

show_debug_message("exists after delete = " + string(path_exists(p)));

// Invalid path tests

show_debug_message("===== INVALID PATH =====");

var bad = 123456789;

show_debug_message("exists = " + string(path_exists(bad)));

show_debug_message("===== PATH TEST END =====");

game_end();