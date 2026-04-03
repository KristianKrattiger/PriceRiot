/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{vue,js,ts,jsx,tsx}"],
  theme: {
    extend: {
      colors: {
        // Backgrounds — layered depth (light, barely lifted)
        background:       "#C7C3C1",
        surface:          "#F0EDE3",
        "surface-hover":  "#E8E4D8",
        "surface-deep":   "#E2DFD1",
        // Borders
        rim:              "#CEC9B6",
        "rim-bright":     "#B5B09C",
        // Accents
        green:            "#008A31",
        mustard:          "#C9980A",
        tangerine:        "#C86800",
        violet:           "#6B56D6",
        danger:           "#A33025",
        "deep-teal":      "#0A3B4D",
        "deep-plum":      "#4D0A3B",
        // Text
        ink:              "#1C1A14",
        "ink-dim":        "#6A6760",
        "ink-ghost":      "#9A9790",
        brick:            "#330018",
        "burnt-orange":   "#A55D00",
        rust:             "#8B5A3C",
        brown:            "#21080D"
      },
      fontFamily: {
        sans: ["IBM Plex Sans", "system-ui", "-apple-system", "sans-serif"],
        mono: ["IBM Plex Mono", "ui-monospace", "Cascadia Code", "monospace"],
      },
      letterSpacing: {
        label: "0.05em",
      },
    },
  },
  plugins: [],
};
