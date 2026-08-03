#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    CanvasDisplayListBuffer, EventValue, UiCommandBuffer, decode_ui_event,
    mount_appspec, pack_result, request_game,
};

const GAME: &[u8] = include_bytes!("../appspec.cbor");

struct Runtime {
    x: [i8; 32],
    y: [i8; 32],
    length: usize,
    direction: u8,
    food_x: i8,
    food_y: i8,
    score: u8,
    game_over: bool,
    cells: [u8; 64],
    score_text: [u8; 3],
    canvas: CanvasDisplayListBuffer<128>,
    commands: UiCommandBuffer<256>,
}

impl Runtime {
    const fn new() -> Self {
        let mut x = [0; 32];
        let mut y = [0; 32];
        x[0] = 3;
        y[0] = 3;
        x[1] = 2;
        y[1] = 3;
        x[2] = 1;
        y[2] = 3;
        Self {
            x,
            y,
            length: 3,
            direction: 1,
            food_x: 5,
            food_y: 3,
            score: 0,
            game_over: false,
            cells: [0; 64],
            score_text: [0; 3],
            canvas: CanvasDisplayListBuffer::new(),
            commands: UiCommandBuffer::new(),
        }
    }

    fn reset(&mut self) {
        *self = Self::new();
    }

    fn turn_left(&mut self) {
        self.direction = (self.direction + 3) % 4;
    }

    fn turn_right(&mut self) {
        self.direction = (self.direction + 1) % 4;
    }

    fn step(&mut self) {
        if self.game_over {
            self.reset();
            return;
        }
        let (dx, dy) = match self.direction {
            0 => (0, -1),
            1 => (1, 0),
            2 => (0, 1),
            _ => (-1, 0),
        };
        let next_x = self.x[0] + dx;
        let next_y = self.y[0] + dy;
        if !(0..8).contains(&next_x) || !(0..8).contains(&next_y) {
            self.game_over = true;
            return;
        }
        for index in 0..self.length {
            if self.x[index] == next_x && self.y[index] == next_y {
                self.game_over = true;
                return;
            }
        }
        let ate = next_x == self.food_x && next_y == self.food_y;
        let next_length = if ate {
            (self.length + 1).min(self.x.len())
        } else {
            self.length
        };
        for index in (1..next_length).rev() {
            self.x[index] = self.x[index - 1];
            self.y[index] = self.y[index - 1];
        }
        self.x[0] = next_x;
        self.y[0] = next_y;
        self.length = next_length;
        if ate {
            self.score = self.score.saturating_add(1);
            const FOOD: [(i8, i8); 5] =
                [(6, 5), (1, 1), (6, 1), (1, 6), (4, 4)];
            let next = FOOD[usize::from(self.score - 1) % FOOD.len()];
            self.food_x = next.0;
            self.food_y = next.1;
        }
    }

    fn render(&mut self) -> u64 {
        self.cells.fill(0);
        self.cells[
            self.food_y as usize * 8 + self.food_x as usize
        ] = 4;
        for index in (0..self.length).rev() {
            let offset =
                self.y[index] as usize * 8 + self.x[index] as usize;
            self.cells[offset] = if index == 0 { 3 } else { 2 };
        }

        if self.canvas.begin().is_err()
            || self.canvas.clear(0).is_err()
            || self
                .canvas
                .rounded_rect(1, 4, 4, 120, 120, 18)
                .is_err()
            || self
                .canvas
                .tile_map(2, 8, 8, 14, 14, 8, 8, &self.cells)
                .is_err()
        {
            return 0;
        }
        let display_list = match self.canvas.finish() {
            Ok(value) => value,
            Err(_) => return 0,
        };

        let mut score_length = 0;
        if self.score >= 10 {
            self.score_text[score_length] = b'0' + self.score / 10;
            score_length += 1;
        }
        self.score_text[score_length] = b'0' + self.score % 10;
        score_length += 1;
        let score = unsafe {
            core::str::from_utf8_unchecked(
                &self.score_text[..score_length],
            )
        };
        if self.commands.begin(3).is_err()
            || self
                .commands
                .set_canvas_display_list(
                    "snake.game.canvas",
                    display_list,
                )
                .is_err()
            || self
                .commands
                .set_primary_text("snake.game.score", score)
                .is_err()
            || self
                .commands
                .set_primary_text(
                    "snake.game.score-label",
                    if self.game_over { "OVER" } else { "SCORE" },
                )
                .is_err()
        {
            return 0;
        }
        match self.commands.finish() {
            Ok(commands) => pack_result(commands),
            Err(_) => 0,
        }
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime =
    SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(GAME);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(
    pointer: *const u8,
    length: u32,
) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    let bytes =
        unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    if request_game("snake.tick", &[]).is_err() {
        return 0;
    }
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    if event.action_id == "snake.step" {
        runtime.step();
        return runtime.render();
    }
    if event.action_id != "snake.control" {
        return 0;
    }
    let control = match event.value {
        EventValue::Text(value) => value,
        _ => return 0,
    };
    match control {
        "L" => {
            runtime.turn_left();
            runtime.step();
        }
        "GO" => runtime.step(),
        "R" => {
            runtime.turn_right();
            runtime.step();
        }
        _ => return 0,
    }
    runtime.render()
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
