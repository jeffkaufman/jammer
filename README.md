# Simple MIDI Mapper for AXIS-49

Run `make run` or download the binary from https://www.jefftk.com/simple-jammer-binary.zip

It looks for an AXIS-49 and presents a virtual midi keyboard called "jammer".

There are two potential orientations for a jammer-layout AXIS-49, and this
implements the one with the transpose keys at the bottom.  For the other one,
look in `handle_button` and switch it from calling
`mapping_transpose_buttons_down` to `mapping_transpose_buttons_up`.
