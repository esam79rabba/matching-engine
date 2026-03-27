#!/usr/bin/env python3
"""Insert exported PNG diagrams into both PowerPoint decks."""

from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
import os

DIAGRAM_DIR = "/Users/esam/Desktop/MatchingEngine/docs/diagrams"
DELIVERY_DIR = "/Users/esam/Desktop/MatchingEngine/docs/delivery"

# ─── Diagram mapping: slide index (0-based) → diagram file ───
# Executive Summary Deck (14 slides)
EXEC_DIAGRAMS = {
    2:  "01-system-context.png",        # Slide 3: Solution
    5:  "06-fr1-vs-enterprise.png",     # Slide 6: FR-1 vs Enterprise
    6:  "07-deployment-profiles.png",   # Slide 7: Deployment Profiles
}

# Architecture Review Deck (30 slides)
ARCH_DIAGRAMS = {
    2:  "01-system-context.png",              # Slide 3: System Context
    3:  "02-logical-architecture.png",        # Slide 4: Logical Architecture
    4:  "04-hot-path-separation.png",         # Slide 5: Hot Path
    6:  "09-fairness-sequencing-boundary.png",# Slide 7: Sequencing/Fairness
    7:  "08-order-lifecycle-sequence.png",     # Slide 8: Order Lifecycle
    10: "10-journaling-replay-recovery.png",  # Slide 11: Journaling
    11: "10-journaling-replay-recovery.png",  # Slide 12: Recovery (same diagram)
    12: "05-participant-connectivity.png",     # Slide 13: Gateway Architecture
    18: "11-ha-dr-failover.png",              # Slide 19: HA/DR
    20: "03-component-interaction.png",       # Slide 21: Component Interaction
    27: "12-rollout-migration-swimlane.png",  # Slide 28: Rollout
}


def remove_placeholder_textboxes(slide):
    """Remove any textbox that contains '[INSERT DIAGRAM]'."""
    shapes_to_remove = []
    for shape in slide.shapes:
        if shape.has_text_frame:
            text = shape.text_frame.text
            if "[INSERT DIAGRAM]" in text:
                shapes_to_remove.append(shape)
    for shape in shapes_to_remove:
        sp = shape._element
        sp.getparent().remove(sp)


def insert_diagram(slide, png_path, position="right"):
    """Insert a diagram PNG into a slide.

    position: 'right' = right half of slide, 'full' = near-full width centered
    """
    if not os.path.exists(png_path):
        print(f"  WARNING: {png_path} not found, skipping")
        return

    # Remove placeholder text
    remove_placeholder_textboxes(slide)

    if position == "right":
        # Right side: bullets on left, diagram on right
        left = Inches(5.8)
        top = Inches(1.2)
        width = Inches(3.8)
        height = Inches(5.0)
    elif position == "full":
        # Near-full width, below title
        left = Inches(0.5)
        top = Inches(1.2)
        width = Inches(9.0)
        height = Inches(5.5)
    elif position == "bottom_right":
        # Smaller, bottom right
        left = Inches(5.5)
        top = Inches(2.5)
        width = Inches(4.0)
        height = Inches(4.0)
    else:
        left = Inches(5.8)
        top = Inches(1.2)
        width = Inches(3.8)
        height = Inches(5.0)

    # Get image dimensions to maintain aspect ratio
    from PIL import Image
    try:
        img = Image.open(png_path)
        img_w, img_h = img.size
        aspect = img_w / img_h

        # Fit within the target box while maintaining aspect ratio
        target_w = width
        target_h = height
        target_aspect = Emu(target_w) / Emu(target_h)

        if aspect > (Emu(target_w) / Emu(target_h)):
            # Image is wider than target box - fit to width
            final_w = target_w
            final_h = int(Emu(target_w) / aspect)
        else:
            # Image is taller than target box - fit to height
            final_h = target_h
            final_w = int(Emu(target_h) * aspect)

        slide.shapes.add_picture(png_path, left, top, final_w, final_h)
    except ImportError:
        # PIL not available, use fixed size
        slide.shapes.add_picture(png_path, left, top, width, height)
    except Exception as e:
        print(f"  WARNING: Error processing {png_path}: {e}")
        # Fallback: insert without aspect ratio preservation
        slide.shapes.add_picture(png_path, left, top, width, height)

    print(f"  Inserted: {os.path.basename(png_path)}")


def process_deck(pptx_path, diagram_map, deck_name):
    """Insert diagrams into a deck and save."""
    print(f"\nProcessing: {deck_name}")
    prs = Presentation(pptx_path)

    for slide_idx, diagram_file in diagram_map.items():
        if slide_idx >= len(prs.slides):
            print(f"  WARNING: Slide index {slide_idx} out of range (deck has {len(prs.slides)} slides)")
            continue

        slide = prs.slides[slide_idx]
        png_path = os.path.join(DIAGRAM_DIR, diagram_file)

        # Determine position based on whether slide has substantial bullet content
        # Architecture deck slides with heavy diagrams get 'right' position
        # Slides that are primarily diagram-focused get 'right' but slightly larger
        insert_diagram(slide, png_path, position="right")

    # Save
    prs.save(pptx_path)
    print(f"  Saved: {pptx_path}")


def main():
    # Check for PIL
    try:
        from PIL import Image
        print("PIL available - will maintain aspect ratios")
    except ImportError:
        print("PIL not available - using fixed dimensions")

    exec_path = os.path.join(DELIVERY_DIR, "Matching-Engine-Executive-Summary-v1.0.pptx")
    arch_path = os.path.join(DELIVERY_DIR, "Matching-Engine-Architecture-Review-v1.0.pptx")

    process_deck(exec_path, EXEC_DIAGRAMS, "Executive Summary Deck")
    process_deck(arch_path, ARCH_DIAGRAMS, "Architecture Review Deck")

    print("\nDone. Diagrams inserted into both decks.")


if __name__ == "__main__":
    main()
