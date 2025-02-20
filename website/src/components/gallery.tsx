import ImageGallery from "react-image-gallery";
import "react-image-gallery/styles/css/image-gallery.css";

const images = [
  {
    original: "/photos/leaf-with-logo.png",
    thumbnail: "/photos/leaf-with-logo.png",
  },
  {
    original: "/photos/flight-deck-2.png",
    thumbnail: "/photos/flight-deck-2.png",
  },
  {
    original: "/photos/leaf-package.png",
    thumbnail: "/photos/leaf-package.png",
  },
  {
    original: "/photos/mounting-options.png",
    thumbnail: "/photos/mounting-options.png",
  },
  {
    original: "/photos/LeafDisplayScreens.png",
    thumbnail: "/photos/LeafDisplayScreens.png",
  },
  {
    original: "/photos/riser.png",
    thumbnail: "/photos/riser.png",
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
