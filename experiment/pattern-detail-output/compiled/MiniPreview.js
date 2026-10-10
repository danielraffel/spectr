function MiniPreview({ gains, w = 56, h = 22 }) {
    const N = gains.length;
    const bw = w / N;
    return /* @__PURE__ */ React.createElement("svg", { width: w, height: h, style: { opacity: 0.9 } },
        gains.map((g, i) => {
            if (g === -Infinity) {
                return /* @__PURE__ */ React.createElement("rect", { key: i, x: i * bw, y: h - 2, width: Math.max(1, bw - 0.5), height: 2, fill: "rgba(255,100,100,0.3)" });
            }
            const gh = Math.max(1, Math.abs(g) * (h - 2));
            const y = g >= 0 ? h / 2 - gh : h / 2;
            const hue = 240 - i / N * 300;
            return /* @__PURE__ */ React.createElement("rect", { key: i, x: i * bw, y: y, width: Math.max(1, bw - 0.5), height: gh, fill: `hsl(${hue}, 75%, 60%)`, opacity: 0.85 });
        }),
        React.createElement("line", { x1: 0, y1: h / 2, x2: w, y2: h / 2, stroke: "rgba(255,255,255,0.15)", strokeWidth: 0.5 }));
}
