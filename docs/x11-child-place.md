# X11 child content placement

Support version 8 of `_KDE_NET_WM_STEREO_CONTENT_SUPPORTED` adds `_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE`.
This property belongs to a child already listed in its top-level window's `_KDE_NET_WM_STEREO_CONTENT_CHILDREN`.
It separates the child's native buffer geometry from its displayed rectangle.
The child's stereo layout continues to use `_KDE_NET_WM_STEREO_CONTENT`; this property does not declare a layout.

The property has type CARDINAL, format 32, and exactly five values: x, y, width, height, below-parent.
Coordinates are relative to the top-level client's origin, in X11 display pixels.
The first two values encode signed 32-bit integers, limited to -32767 through 32767.
Width and height are 1 through 32767.
Below-parent is either 0 (the existing above-parent stacking) or 1 (below the ordinary top-level client surface).
Deleting the property restores the existing native-child geometry and above-parent stacking.
Absent, malformed or out-of-range properties have the same effect as deletion.

For stereo content, the child buffer holds the left and right eyes side by side at their stored resolution.
The declared rectangle is the place of one displayed eye.
The compositor scales each eye once to this rectangle.
For ordinary content, the whole child buffer occupies the rectangle identically in both eyes.
A child placed below the parent is visible through transparent parts of the parent's buffer; subtitles and overlays stay in that ordinary parent buffer at display resolution.

This does not change X11 input geometry.
A client using a placement different from its native child geometry must make the child's input shape empty and handle input in the logical parent area.
Parent visibility, lifetime and clipping still constrain the child.
The client must test support version 8 before writing this property.
Older compositors continue to use the existing native child contract.
