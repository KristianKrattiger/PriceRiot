/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{vue,js,ts,jsx,tsx}"],
  theme: {
    extend: {
      colors: {
        background: "#020617",
        surface: "#020a13",
        accent: "#22c55e"
      }
    }
  },
  plugins: []
};

