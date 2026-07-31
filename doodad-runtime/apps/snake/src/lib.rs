#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, UiCommandBuffer, decode_ui_event, mount_appspec, pack_result,
    request_game,
};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const GAME: &[u8] = include_bytes!("../screens/game.cbor");

struct Runtime {
    x: [i8; 32],
    y: [i8; 32],
    length: usize,
    direction: u8,
    food_x: i8,
    food_y: i8,
    score: u8,
    game_over: bool,
    board: [u8; 96],
    score_text: [u8; 24],
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
            board: [0; 96],
            score_text: [0; 24],
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
        let mut cursor = 0;
        for row in 0..8 {
            for column in 0..8 {
                let mut value = if column == self.food_x && row == self.food_y {
                    b'*'
                } else {
                    b'.'
                };
                for index in 0..self.length {
                    if self.x[index] == column && self.y[index] == row {
                        value = if index == 0 { b'@' } else { b'o' };
                        break;
                    }
                }
                self.board[cursor] = value;
                cursor += 1;
            }
            if row != 7 {
                self.board[cursor] = b'\n';
                cursor += 1;
            }
        }
        let prefix = if self.game_over {
            b"GAME OVER " as &[u8]
        } else {
            b"SCORE " as &[u8]
        };
        self.score_text[..prefix.len()].copy_from_slice(prefix);
        let mut score_length = prefix.len();
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
        let top =
            unsafe { core::str::from_utf8_unchecked(&self.board[..35]) };
        let bottom = unsafe {
            core::str::from_utf8_unchecked(&self.board[36..cursor])
        };
        if self.commands.begin(3).is_err()
            || self
                .commands
                .set_primary_text("snake.game.board-top", top)
                .is_err()
            || self
                .commands
                .set_primary_text("snake.game.board-bottom", bottom)
                .is_err()
            || self
                .commands
                .set_primary_text("snake.game.score", score)
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
    let _ = mount_appspec(HOME);
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
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    if event.action_id == "snake.primary" {
        if request_game("snake.start", &[]).is_err() {
            return 0;
        }
        runtime.reset();
        let _ = mount_appspec(GAME);
        return runtime.render();
    }
    if event.action_id != "snake.control" {
        return 0;
    }
    let control = match event.value {
        EventValue::Text(value) => value,
        _ => return 0,
    };
    if request_game("snake.tick", &[]).is_err() {
        return 0;
    }
    match control {
        "L" => {
            runtime.turn_left();
            runtime.step();
        }
        "Go" => runtime.step(),
        "R" => {
            runtime.turn_right();
            runtime.step();
        }
        "New" => runtime.reset(),
        "Quit" => {
            let _ = mount_appspec(HOME);
            return 0;
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
