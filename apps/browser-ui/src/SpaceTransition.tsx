import { Component, createRef, type ReactNode, type RefObject } from 'react';

type Props = {
  spaceId: string; position: number; hidden: boolean;
  scrollRef: RefObject<HTMLDivElement | null>;
  layerRef: RefObject<HTMLDivElement | null>;
  children: ReactNode;
};
type Frame = { picture: HTMLDivElement; scrollTop: number; direction: number; paddingRight: number };

/** Capture the outgoing DOM before React replaces it. Shared Favorites and
 * the space heading live outside this viewport and never join the slide. */
export class SpaceTransition extends Component<Props, Record<string, never>, Frame | null> {
  private viewport = createRef<HTMLDivElement>();
  private animations: Animation[] = [];
  private picture?: HTMLDivElement;
  private gutterObserver?: ResizeObserver;

  componentDidMount() {
    this.measureGutter();
    this.gutterObserver = new ResizeObserver(() => this.measureGutter());
    if (this.props.scrollRef.current) this.gutterObserver.observe(this.props.scrollRef.current);
  }

  private measureGutter() {
    const scroll = this.props.scrollRef.current;
    if (!scroll) return;
    this.viewport.current?.style.setProperty('--sidebar-scrollbar-size', `${scroll.offsetWidth - scroll.clientWidth}px`);
    // At the page edge: use the whole gutter when empty, only the scrollbar's
    // outer transparent 2px when it is present, leaving the thumb draggable.
    this.viewport.current?.closest<HTMLElement>('.workspace, .peek-root')?.style.setProperty('--sidebar-resize-hit-width',
      scroll.scrollHeight > scroll.clientHeight ? '2px' : '8px');
  }

  getSnapshotBeforeUpdate(previous: Props): Frame | null {
    const scroll = this.props.scrollRef.current;
    if (previous.spaceId === this.props.spaceId || !previous.spaceId || previous.hidden || this.props.hidden || !scroll ||
        matchMedia('(prefers-reduced-motion: reduce)').matches) return null;
    return { picture: scroll.cloneNode(true) as HTMLDivElement, scrollTop: scroll.scrollTop,
      paddingRight: parseFloat(getComputedStyle(scroll).paddingRight) + scroll.offsetWidth - scroll.clientWidth,
      direction: this.props.position > previous.position ? 1 : -1 };
  }

  componentDidUpdate(previous: Props, _state: Record<string, never>, frame: Frame | null) {
    this.measureGutter();
    if (previous.spaceId !== this.props.spaceId || this.props.hidden) this.stop();
    const scroll = this.props.scrollRef.current, viewport = this.viewport.current;
    if (!frame || !scroll || !viewport) return;
    const { picture, direction } = frame;
    picture.classList.add('space-transition-outgoing');
    // Its scrollbar is hidden, but its reserved gutter must not widen rows.
    picture.style.paddingRight = `${frame.paddingRight}px`;
    picture.inert = true; picture.setAttribute('aria-hidden', 'true');
    viewport.append(picture); picture.scrollTop = frame.scrollTop;
    this.picture = picture;
    scroll.scrollTop = 0;
    const distance = viewport.clientWidth;
    const options = { duration: 320, easing: 'cubic-bezier(.22, .75, .2, 1)' };
    const outgoing = picture.animate([
      { transform: 'none', opacity: 1 },
      { transform: `translateX(${-direction * distance}px)`, opacity: .55 },
    ], options);
    const incoming = scroll.animate([
      { transform: `translateX(${direction * distance}px)`, opacity: .55 },
      { transform: 'none', opacity: 1 },
    ], options);
    this.animations = [outgoing, incoming];
    void outgoing.finished.catch(() => {}).then(() => {
      picture.remove();
      if (this.picture === picture) { this.picture = undefined; this.animations = []; }
    });
  }

  componentWillUnmount() { this.gutterObserver?.disconnect(); this.stop(); }

  private stop() {
    for (const animation of this.animations) animation.cancel();
    this.animations = []; this.picture?.remove(); this.picture = undefined;
  }

  render() {
    return <div className="sidebar-list" ref={this.viewport}>
      <div className="sidebar-scroll" ref={this.props.scrollRef}>{this.props.children}</div>
      <div className="sidebar-motion-layer" ref={this.props.layerRef} aria-hidden="true" />
    </div>;
  }
}
