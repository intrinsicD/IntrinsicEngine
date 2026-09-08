I'll check two conventions in the shared helpers that the inline code depends on, then give the review.


<invoke name="Grep">
<invoke name="pattern">def geometry|def incidence|center|scale</invoke>
<invoke name="path">/home/alex/Documents/IntrinsicEngine/tools/diagnostics/atlas/patch_merge.py</invoke>
<invoke name="output_mode">content</invoke>
<invoke name="-n">true</invoke>
<invoke name="head_limit">40</invoke>
</invoke>
<invoke name="Grep">
<invoke name="pattern">def incidence|def open_region|def boundary_metrics|edges\[|sorted\(\(</invoke>
<invoke name="path">/home/alex/Documents/IntrinsicEngine/tools/diagnostics/atlas/baseline_atlas.py</invoke>
<invoke name="output_mode">content</invoke>
<invoke name="-n">true</invoke>
<invoke name="head_limit">30</invoke>
</invoke>
</invoke>
