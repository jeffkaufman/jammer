# Map an Axis 49 to a Wicki-Hayden layout

Run `make run` to build this software and run it.  It will look for an Axis 49
and will present a virtual midi device called `jammer`.

It should detect whether the jammer is in selfless mode and map accordingly.

In non-selfless mode you need to transpose up (I think) once before all the
keys will work, because I accidentally did the mapping while transposed.

It also looks for a TEControl breath controller, and if it finds one it remaps
it from CC-2 to CC-11.
