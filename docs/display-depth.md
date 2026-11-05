# The depth of a display: one source

`stereo_policy_display_depth()` in libstereo-policy answers one question for every program that puts depth on a stereo output:
how far may it sink behind the screen and pop in front of it, on this display, for this person. Stereo KWin reads it for the
desktop's windows, the browser reads it for WebXR pages on the flat stereo display, and the CSS depth levels (sunk, screen,
popped) will read it, so a page's popped element and the desktop's popped menu agree on the same screen.

Read in this order of importance (Daniel, 2026-11-05): (1) the person's choice, built on (2) the display.

## The display's default

An angular budget, the same on every display of one kind, so a framed screen of any size seen from its own distance gets the
parallax that looks alike. The reference is the desktop's own default, 10 pixels sunk and 4 popped on a 1920 wide view of a 24 inch
monitor (531 x 299 mm) seen from 0.8 m: 0.198 and 0.079 degrees of disparity at the eyes.

- **Viewing distance** from the physical size: 0.6 m up to a diagonal of 0.5 m, then 1.8 m more for each metre of diagonal
  (24 inch monitor 797 mm, 46 inch television 1.8 m, 65 inch television 2.7 m, a laptop 0.6 m).
- **Pixels at a view 1920 wide** keep the angle: the reference limits times (viewing distance / 797 mm) times (531 mm / width).
  A 46 inch television gets 11.8 and 4.7, a 65 inch one 12.4 and 5.0, a laptop 11.6 and 4.6: framed screens of any size stay
  within a pixel or two of each other, because they subtend alike.
- **Kind**: a framed screen seen through glasses (television, monitor, projector) 1.0; a screen shown in colours (anaglyph) 0.7,
  as ghosting grows with parallax; a headset (a flat headset, or a phone in one) 2.5 at a window distance of 0.7 m whatever the
  size (about 2.2 times the reference monitor): headsets get more separation, framed screens less.
- A size that is not known (width or height 0) stands in as the 24 inch monitor and `size_known` says 0, so the program can tell
  the person; nothing is guessed silently.

## The person's choice

A strength in percent of the display's default (100) and, for either limit, an absolute value in pixels at 1920 that replaces it
(0: none). Strength 0 switches depth off.

These numbers are the edition's, not a standard's: no recorded design gives them (listed for Daniel). The angular rule and the
order of precedence are Daniel's words quoted above.
