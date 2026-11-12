/** Moves focus to an element when it mounts: a ref callback for the field a dialog or a page opens on.
 *  It does what the autoFocus attribute did (focus once at mount, nothing at later renders), and says so
 *  where the intent is instead of leaving it to an attribute that reads as a stray focus grab. */
export const focusOnMount = (element: HTMLElement | null) => { element?.focus(); };
