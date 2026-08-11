# Thermal Core Tutorial Script

Edit this script first, then update `website/public/labs/thermal-core-simulator/index.html` to match.

During each step, keep the tutorial card text constant after the user starts. In-flight guidance should appear as map pop-ups, not as a second phase of tutorial-card text.

Whenever an in-flight vario guidance pop-up would be shown, net vario <= 0 overrides all prior guidance with **FIND LIFT**.

## Repeated Three-Step Lesson

### Step 1

**Title:** Step 1: Find the "halfway point" into the thermal

**Directions:** Fly straight into the lift. Don't turn. When the vario peaks and then starts to decrease, you are about halfway through the core. That is the moment to start turning.

**Goal:** Fly straight, wait for the vario to peak, then turn about 90 degrees.

**Flying Pop Up:** START TURNING NOW

Turn about 90 degrees

**Pop Up Trigger:** When the vario has exceeded 1.0 m/s, the run has lasted at least 6 seconds, and the current vario has dropped at least 0.25 m/s below the peak seen so far.

**End Condition:** User has flown past a vario peak and turned >75 degrees.

**Completion Text:** Good. You found the halfway point and started the turn soon after the vario peak.

**UI:** Hide all turn-rate colors.

### Step 2

**Title:** Step 2: Turn around the center of the thermal

**Directions:** Now the goal is to keep the vario constant. If it starts to drop, you are moving away from the center and need to tighten your turn. If it starts to increase, you are moving toward the center and need to widen your turn.

**Goal:** Fly 2 consecutive smooth orbits around the thermal. Don't get too close to the center or the edge.

**Flying Pop Up:** Either 1 "VARIO STEADY - hold this turn rate" 2 "VARIO INCREASING - widen your turn to move away from the center" 3 "VARIO DECREASING - tighten your turn to keep closer to the center"

**Pop Up Trigger:** Either 1 [Vario not changing by more than .15m/s/s] 2 [vario increasing more than .15m/s/s] 3 [vario decreasing by more than .15m/s/s]

**End Condition:** 2 consecutive successful orbits. [success criteria: user stayed within 10% and 75% of thermal radius for the orbits]

**Completion Text:** Good. You flew two centered orbits without drifting too close to the middle or the edge.

**UI:** Hide all turn-rate colors.

### Step 3

**Title:** Step 3: Optimize your turn rate

**Directions:** The green turn-rate bands show the efficient turning radius. Turning too steep will increase your descent rate, so don't stay in a steep turn for long. Try to center your circles around the thermal and stay as close to the optimal turn rate as you can.

**Goal:** Fly 2 consecutive orbits staying near optimal turn-rate while staying centered on the thermal.

**Flying Pop Up:** Either 1 "CENTER THE THERMAL - listen to the vario changes and adjust your turns to line up with the circle center" 2 "SMOOTH OUT THE TURN - use a consistent turn rate near the optimum rate"

**Trigger:** Either 1 [turn-rate is within 25% of target at least 75% of the orbit so far, but radius variation is more than 15% of thermal radius] 2 [radius variation is within 15% of thermal radius, but turn-rate is not within 25% of target at least 75% of the orbit so far]

**End Condition:** 2 consecutive successful orbits where radius doesn't vary by more than 15% of the thermal radius, and turn-rate is within 25% of the target turn rate at least 75% of the time.

**Completion Text:** Good. You stayed centered while keeping the turn rate near the efficient range.

**Hint:** In Stage Two or Stage Three only, if the user has not completed this step after 1 minute, show a small Leaf-green "? tip" bubble above the turn-rate indicator. Clicking it pauses the sim and opens: "Pro Tip: Keep your ears on the vario beeping, and your eyes on the turn rate indicator. Try to generally hold near the optimum turn rate. If the vario starts to decrease, tighten your turn briefly until the vario feels roughly constant, then quickly come back to the optimum turn rate. If the vario starts to increase, widen your turn briefly until the vario feels roughly constant, then come back to the optimum turn rate. You're trying to make subtle shifts to your circles while holding near the optimum turn rate most of the time. By keeping your eyes on the turn rate, you're teaching yourself the exact same response you'll use when thermaling in a real paraglider." Dismissing the tip resumes the sim. For testing, pressing T forces the tip to appear.

**UI:** Show turn-rate colors.

## Tutorial Stage One

**Card Title:** TUTORIAL STAGE ONE

**Visual References:** Show fuzzy green thermal, outer boundary, and concentric rings.

**Steps:** Run Step 1, Step 2, and Step 3.

**After Step 3:** Great job, you centered the thermal at the optimum turn rate. Next, practice again without the visual circle help. The fuzzy green thermal is still shown, but rely on the vario sound for most of your guidance.

## Tutorial Stage Two

**Card Title:** TUTORIAL STAGE TWO

**Visual References:** Show fuzzy green thermal only. Hide outer boundary and concentric rings.

**Steps:** Run the exact same Step 1, Step 2, and Step 3.

**After Step 3:** Now let's try again without any visual references. Just as in real-life paragliding, you'll only have the vario to guide you, so listen closely!

## Tutorial Stage Three

**Card Title:** TUTORIAL STAGE THREE

**Visual References:** Hide thermal visual, outer boundary, and concentric rings.

**Steps:** Run the exact same Step 1, Step 2, and Step 3.

**After Step 3:** Congratulations, you used only the vario to core a thermal! Now you may want to try some of the free fly scenarios that add thermal variability and wind!

## Tutorial Stage Four

**Card Title:** TUTORIAL STAGE FOUR

**Theme:** Understanding Thermal Sizes

### Step 1

**Title:** Understanding Thermal Sizes

**Directions:** The previous thermal we explored required us to fly straight into it for 7 to 8 seconds before we detected the middle using the vario peak. Count how many seconds it takes to find the middle of this thermal, then try to circle it like before.

**Goal:** Fly straight, wait for the vario to peak, then turn about 90 degrees and try to stay in lift for 2 consecutive orbits.

**Flying Pop Up:** START TURNING NOW

Turn about 90 degrees

**Pop Up Trigger:** When the vario has exceeded 1.0 m/s, the run has lasted at least 6 seconds, and the current vario has dropped at least 0.25 m/s below the peak seen so far.

**Flow:** Continuous. After the user turns about 90 degrees, continue directly into orbit practice without pausing.

**End Condition:** 2 consecutive successful orbits where the vario stays positive throughout each orbit.

**Completion Text:** Great job coring a small thermal.  At this small size, it only took ~4 seconds between entering the thermal and finding the vario peak.  

**UI:** Use an 80 m thermal. Show fuzzy green thermal, outer boundary, concentric rings, and turn-rate colors.

### Step 2

**Title:** Step 2: Small thermal without circle guidance

**Directions:** Try the same small thermal again, this time without the circle guidance. Use the fuzzy green thermal and the vario trend to stay in lift.  Remember to practice counting seconds until the peak to get a sense for how large a thermal might be.

**Goal:** Fly straight, wait for the vario to peak, then turn about 90 degrees and try to stay in lift.

**Flying Pop Up:** START TURNING NOW

Turn about 90 degrees

**Flow:** Continuous. After the user turns about 90 degrees, continue directly into orbit practice without pausing.

**End Condition:** 2 consecutive successful orbits where the vario stays positive throughout each orbit, or a 3 minute timeout.

**Completion Text: (success)** Great job.  Without the guide rings, the same small lift takes more precise adjustments.

**Timeout Text:** Time limit reached. It's much harder to stay in lift when a thermal is small.

**UI:** Use the same 80 m thermal setup. Show fuzzy green thermal and turn-rate colors. Hide outer boundary and concentric rings.

### Step 3

**Title:** Step 3: Small thermal by vario only

**Directions:** Now try the same small thermal with no visual help. In real flight, you only have the vario to go on.  For practice, keep counting seconds until the peak.  But eventually you'll start to get a "feel" for the size of the thermal without counting.

**Goal:** Fly straight, wait for the vario to peak, then turn about 90 degrees and try to stay in lift.

**Flying Pop Up:** START TURNING NOW

Turn about 90 degrees

**Flow:** Continuous. After the user turns about 90 degrees, continue directly into orbit practice without pausing.

**End Condition:** 2 consecutive successful orbits where the vario stays positive throughout each orbit, or a 3 minute timeout.

**Completion Text:** Great job!  This is a good lesson that a ~4 second thermal might be about the smallest you can stay in.  Any smaller and it might be worth passing by until you find a larger thermal.

**Timeout Text:** Time limit reached. As your thermal skills improve, you may be able to stay in lift this small. But this is a good exercise to determine what thermals are worth turning in, and which ones might be worth passing by.

**UI:** Use the same 80 m thermal setup. Show turn-rate colors. Hide thermal visual, outer boundary, and concentric rings.

## Follow-Up Free Fly

- Easy default scenario loads when the page opens.
- Medium, Hard, and Expert add thermal variability and wind.
- Future drills: random invisible thermal, repeat same invisible thermal, center/edge entry offsets, and crossing entries from different approach angles.
