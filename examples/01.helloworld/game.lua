
--[[
   This is a simple example that simply loads an image
   and then displays it to the user in a 640x480 window.

   Moreover, the user can close the window.

   You can copy, alter or reuse this file any way you like.
   It is available without restrictions (public domain).
]] --

print("Hello world example in blitwizard")

-- All blitwiz.callback.* functions are predetermined by
-- blitwizard and are called when various things happen
-- (if you add them to your program). This allows you to
-- react to certain events (e.g. program has loaded, window
-- is redrawn etc.).

-- For more information on which callback functions are
-- available, check the other examples or see the full
-- documentation on http://games.homeofjones.de/blitwizard/

function blitwiz.callback.load()
	-- This function is called right after blitwizard has
	-- started. You would normally want to open up a
	-- window here with blitwiz.graphics.setWindow().
	
	-- Open a window
	blitwiz.graphics.setWindow(640,480,"Hello World")

	-- Ask for an image to be loaded
	imageLoaded = false
	blitwiz.graphics.loadImage("hello_world.png")

	-- Please note the image won't be available instantly.
	-- Images are loaded in the background, so take some time
	-- before they're available (in that time, you can continue
	-- to do other things, e.g. showing a loading screen image
	-- you have already loaded).
	-- The function blitwiz.callback.image() will be called as
	-- soon as it has finished loading (see below).
end

function blitwiz.callback.draw()
	-- This gets called each time the window is redrawn.
	-- If you don't put this function here or if you don't put
	-- any drawing calls into it, the window will simply stay
	-- black.
	
	-- We have to check whether our image is loaded first,
	-- since image loading in blitwizard doesn't hang your
	-- whole application, but instead happens in the background:
	if imageLoaded == true then
		-- When the image is loaded, draw it centered:
		local w,h = blitwiz.graphics.getImageSize("hello_world.png")
		local mw,mh = blitwiz.graphics.getWindowSize()

		-- Actual drawing happens here
		blitwiz.graphics.drawImage("hello_world.png", mw/2 - w/2, mh/2 - h/2)
	end

	-- Done!
end

function blitwiz.callback.event.image(name, success)
	-- This gets called every time an image has been loaded.
	-- You would normally set some variables e.g. to advance
	-- a loading progress bar or to tell your drawing code that
	-- various images are available for use now.
	
	if name == "hello_world.png" then
		-- Hey, it is our requested image!
		if success then
			-- Loading succeeded. So we can use it now
			imgLoaded = true
		else
			-- Loading failed. So we quit with an error:
			print("Error: Failed to load hello_world.png!")
			blitwiz.quit()
		end
	end		
end

function blitwiz.callback.event.close()
	-- This function gets called whenever the user clicks
	-- the close button in the window title bar.
	
	-- The user has attempted to close the window,
	-- so we want to respect his wishes and quit :-)
	blitwiz.quit()
end

function blitwiz.callback.step()
	-- This gets called with fixed 60 FPS constantly (it
	-- will get called more often if FPS are lower to
	-- keep up).
	
	-- This is the right place for game physics/continuous
	-- movements or calculations of any sort that are part
	-- of your continuously running game simulation.

	-- For this example, we just want to quit after 5 seconds
	if blitwiz.time.getTime() > 5000 then
		blitwiz.quit()
	end
end
