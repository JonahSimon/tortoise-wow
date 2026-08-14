Speaking to the owner of the fork above our own, they have been able to optimize the memory usage of the system that drives the bots. Boasting being able to run 2000 bots with only 1gb of ram.

"I'm currently working from scratch on a new bot module that doesn't use as much RAM, but it's still in the early stages. I have over 2,000 bots running on my second Turtle server, and it's using 1 GB of RAM 😄 That said, RAM usage on the “old” Turtle isn't that high anymore either, I've managed to reduce it quite a bit."

The goal of this idea is to explore how we can optimize the system the ram usage. The optimizations that have been uncovered have not been shared with me yet, and as im unfamiliar with how things are setup. so im not sure if its configuration, optimization of code, or some other nuance that theyve discovered.

Your goal, should be to analyze the system as a whole to understand the nuances behind how it works, develop a gameplan for how you can optimize the memory usage better, with the goal that we want to run 2000 bots with only 1gb of memory used.

Its worth calling out, that your goal, should not be to decrease the capabilities of the AI, my other goals are to explore how we can make them smarter and more optimized in the world, if the solution to save memory affects their ability to function, and by ability to function, i mean the ability to play the game like another player. If the changes involve re-writing systems of the code to optimize and remove issues, without removing functionality, that is falls into an acceptable change. 

Docker is availbable for you to use, spin up a stack before making any changes to capture a baseline for memory usage with 1000 bots running, if the server crashes, scale down until it stops crashing to capture the baseline

After capturing a baseline, implement the fixes, and upon completion, run another test to capture the usage at 1000 bots running to see if it had any noticeable impact. Experiement with other bot counts that you think would be helpful to determine if the changes help or not

Given the nature of these changes, i suspect that most PRs will be layered.

If testing looks promising, perform a test with 3000 bots, report on stability and overall quality of the server.