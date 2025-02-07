import ImageGallery from "react-image-gallery";
import "react-image-gallery/styles/css/image-gallery.css";

const images = [
  {
    original: "/photos/leaf-with-logo.png",
    thumbnail: "/photos/leaf-with-logo.png",
  },
  {
    original: "/photos/flight-deck.png",
    thumbnail: "/photos/flight-deck.png",
  },
  {
    original: "/photos/riser.png",
    thumbnail: "/photos/riser.png",
  },
  {
    original: "/photos/caddy.png",
    thumbnail: "/photos/caddy.png",
  },
  {
    original: "/photos/hardware.png",
    thumbnail: "/photos/hardware.png",
  },

];

export default function Gallery(props: { base: string }) {
  const { base } = props;
  const imagesWithBase = images.map(i => {
    return {
      ...i,
      original: base + i.original,
      thumbnail: base + i.thumbnail,
    }
  });

  return <ImageGallery items={imagesWithBase} />;
}
