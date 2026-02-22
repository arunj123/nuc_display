#!/usr/bin/env python3
import os
import subprocess
import sys

# Directory setup
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG_DIR = os.path.join(BASE_DIR, "assets", "weather_svg")
PNG_DIR = os.path.join(BASE_DIR, "assets", "weather")

os.makedirs(SVG_DIR, exist_ok=True)
os.makedirs(PNG_DIR, exist_ok=True)

# Define simple transparent vector graphics for weather icons
svgs = {
    "clear.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <circle cx="128" cy="128" r="60" fill="#FFD700"/>
  <g stroke="#FFD700" stroke-width="12" stroke-linecap="round">
    <line x1="128" y1="20" x2="128" y2="40"/><line x1="128" y1="216" x2="128" y2="236"/>
    <line x1="20" y1="128" x2="40" y2="128"/><line x1="216" y1="128" x2="236" y2="128"/>
    <line x1="51" y1="51" x2="66" y2="66"/><line x1="190" y1="190" x2="205" y2="205"/>
    <line x1="51" y1="205" x2="66" y2="190"/><line x1="190" y1="66" x2="205" y2="51"/>
  </g>
</svg>""",
    "cloudy.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <path d="M70,180 A50,50 0 0,1 70,80 A60,60 0 0,1 180,80 A50,50 0 0,1 180,180 Z" fill="#D3D3D3"/>
</svg>""",
    "fog.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <g stroke="#A9A9A9" stroke-width="16" stroke-linecap="round">
    <line x1="40" y1="100" x2="216" y2="100"/>
    <line x1="80" y1="140" x2="176" y2="140"/>
    <line x1="40" y1="180" x2="216" y2="180"/>
  </g>
</svg>""",
    "drizzle.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <path d="M70,160 A50,50 0 0,1 70,60 A60,60 0 0,1 180,60 A50,50 0 0,1 180,160 Z" fill="#C0C0C0"/>
  <g stroke="#00BFFF" stroke-width="8" stroke-linecap="round">
    <line x1="90" y1="170" x2="80" y2="190"/>
    <line x1="130" y1="170" x2="120" y2="190"/>
    <line x1="170" y1="170" x2="160" y2="190"/>
  </g>
</svg>""",
    "rain.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <path d="M70,150 A50,50 0 0,1 70,50 A60,60 0 0,1 180,50 A50,50 0 0,1 180,150 Z" fill="#A9A9A9"/>
  <g stroke="#1E90FF" stroke-width="12" stroke-linecap="round">
    <line x1="90" y1="160" x2="70" y2="200"/>
    <line x1="130" y1="160" x2="110" y2="200"/>
    <line x1="170" y1="160" x2="150" y2="200"/>
  </g>
</svg>""",
    "snow.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <path d="M70,160 A50,50 0 0,1 70,60 A60,60 0 0,1 180,60 A50,50 0 0,1 180,160 Z" fill="#D3D3D3"/>
  <g fill="#FFFFFF" stroke="#00BFFF" stroke-width="2">
    <circle cx="90" cy="190" r="10"/>
    <circle cx="130" cy="180" r="10"/>
    <circle cx="170" cy="190" r="10"/>
  </g>
</svg>""",
    "storm.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <path d="M70,150 A50,50 0 0,1 70,50 A60,60 0 0,1 180,50 A50,50 0 0,1 180,150 Z" fill="#696969"/>
  <polygon points="140,140 110,190 130,190 110,240 160,180 140,180" fill="#FFD700"/>
</svg>""",
    "unknown.svg": """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <circle cx="128" cy="128" r="80" fill="none" stroke="#A9A9A9" stroke-width="16"/>
  <text x="128" y="160" font-size="96" font-family="sans-serif" font-weight="bold" fill="#A9A9A9" text-anchor="middle">?</text>
</svg>"""
}

def generate_assets():
    print("Generating transparent Weather SVGs and PNGs...")
    for name, content in svgs.items():
        svg_path = os.path.join(SVG_DIR, name)
        with open(svg_path, "w") as f:
            f.write(content)
        
        png_name = name.replace('.svg', '.png')
        png_path = os.path.join(PNG_DIR, png_name)
        
        # Determine generator tool
        cmd = ["rsvg-convert", "-w", "256", "-h", "256", svg_path, "-o", png_path]
        try:
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"Generated {png_name} (using rsvg-convert)")
        except FileNotFoundError:
            try:
                # Fallback to cairosvg if rsvg is unavailable
                import cairosvg
                cairosvg.svg2png(url=svg_path, write_to=png_path, parent_width=256, parent_height=256)
                print(f"Generated {png_name} (using cairosvg)")
            except ImportError:
                print("=========================================\n"
                      "ERROR: SVG backend missing.\n"
                      "To generate transparent PNG icons during build, you must install an SVG renderer:\n"
                      "   Option A: sudo apt-get install librsvg2-bin\n"
                      "   Option B: pip install cairosvg\n"
                      "=========================================", file=sys.stderr)
                sys.exit(1)

if __name__ == "__main__":
    generate_assets()
