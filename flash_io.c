#include <hardware/flash.h>
#include <hardware/sync.h>

#include <stdio.h>
#include <string.h>

/*
 *   The main purpose of saving user data to flash memory is to
 *   handle unexpected loss of electrical power.
 *   In such a case, the furnace driver can boot itself up
 *   with previous config.
 *
 *   In order to read from flash,
 *   config (both size and initialized targets) must be matching.
 *
 *   Also, we are reading from flash if and only if
 *   the last record is the same as current config.
 *   It is done in order to NOT boot with obsolete data.
 *
 *  memory layout:
 *    size
 *    targets
 *    pilot.des_temp
 *    pilot.is_enabled
 *    log_bits
 *    pwm_water
 *
 *    It is worth to note that you cannot rewrite flash memory,
 *    once you write something in it,
 *    you cannot overwrite this without erasing whole flash sector.
 *    It is not described in pico-sdk.
 *    It results from NOR/NAND flash memory.
 */

enum __attribute__((packed))
init_targets {
  MAGNETRON = 1 << 0,
  WATER     = 1 << 1
};

// get symbols from the linker
extern size_t
FLASH_USER_DATA_START;

extern size_t
FLASH_USER_DATA_END;

static const uint8_t
FLASH_USER_DATA_LEN = sizeof(flash_io_t);

static const void*
flash_ptr;

static const enum init_targets
targets = 0
  #if CONFIG_MAGNETRON
    + MAGNETRON
  #endif
  #if CONFIG_WATER
    + WATER
  #endif
;

static bool
check_end_of_user_flash()
{
  if(flash_ptr == &FLASH_USER_DATA_END)
  {
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase((uint32_t)&FLASH_USER_DATA_START - XIP_BASE, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    flash_ptr = &FLASH_USER_DATA_START;
    return true;
  }
  return false;
}

static void
write_flash(flash_io_t* flash_io)
{
  uint8_t buf[FLASH_PAGE_SIZE];
  memset(buf, 0, FLASH_PAGE_SIZE);
  buf[0] = FLASH_USER_DATA_LEN;
  buf[1] = targets;

  memcpy((void*)&buf[2], (void*)flash_io, (size_t)FLASH_USER_DATA_LEN);

  check_end_of_user_flash();

  uint32_t ints = save_and_disable_interrupts();
  flash_range_program((uint32_t)flash_ptr - XIP_BASE, buf, FLASH_PAGE_SIZE);
  restore_interrupts(ints);

  flash_ptr += FLASH_PAGE_SIZE;
}

static int
flash_compare_diffs(furnace_context_t* ctx)
{
  ctx->flash_io.des_temp   = ctx->pilot.des_temp;
  ctx->flash_io.is_enabled = ctx->pilot.is_enabled;
  ctx->flash_io.log_bits   = ctx->log_bits;

#if CONFIG_WATER
  ctx->flash_io.pwm_water  = ctx->pwm_water;
#endif

  bool diff = memcmp(&ctx->flash_io, &ctx->flash_last_written, FLASH_USER_DATA_LEN);

  if(diff != 0)
    memcpy(&(ctx->flash_last_written), &(ctx->flash_io), FLASH_USER_DATA_LEN);

 return diff;
}

static void
read_flash_data(furnace_context_t* ctx)
{
  const void* tmp = flash_ptr;

  // skip size (uint8_t) and targets
  tmp += sizeof(uint8_t);
  tmp += sizeof(enum init_targets);

  memcpy(&(ctx->flash_io), tmp, FLASH_USER_DATA_LEN);
  memcpy(&(ctx->flash_last_written), &(ctx->flash_io), FLASH_USER_DATA_LEN);

  ctx->pilot.des_temp      = ctx->flash_io.des_temp;
  ctx->pilot.is_enabled    = ctx->flash_io.is_enabled;
  ctx->log_bits            = ctx->flash_io.log_bits;

#if CONFIG_WATER
  ctx->pwm_water = ctx->flash_io.pwm_water;
#endif
}

static void
check_for_target(const void** last_correct_page)
{
  const void* tmp = flash_ptr;
  tmp += sizeof(uint8_t);
  const enum init_targets saved_targets = *(enum init_targets*)tmp;

  if(targets == saved_targets)
    *last_correct_page = flash_ptr;
  else
    *last_correct_page = NULL;
}

static void
init_flash(furnace_context_t* ctx)
{
  // There is little chance for us to ever reach 256 (FLASH_PAGE_SIZE) bytes of
  // size of flash_io_t.
  // Also, 255 is reserved.
  // We need to distinct the difference between size and empty memory block.
  //
  // Although, if we pass this number, we must get compile-time error.
  // In such a case, this algorithm is useless,
  // and we will need to develop a new one.
  //
  // We assume that sizeof(enum init_targets) is 1 byte.
  static_assert(FLASH_USER_DATA_LEN < FLASH_PAGE_SIZE - 2, "flash_io_t is too large\n");
  static_assert(sizeof(enum init_targets) == 1, "init_targets size is different than assumed\n");

  flash_ptr = &FLASH_USER_DATA_START;
  uint8_t size;
  const void* last_correct_page = NULL;

  while(true)
  {
    if(check_end_of_user_flash())
      break;

    size = *(uint8_t*)flash_ptr;

    if(size == FLASH_USER_DATA_LEN)
      check_for_target(&last_correct_page);
    else if((int8_t)size == -1)
    {
      if(last_correct_page != NULL)
      {
        flash_ptr = last_correct_page;
        read_flash_data(ctx);
        flash_ptr += FLASH_PAGE_SIZE;
      }

      break;
    }
    else
      last_correct_page = NULL;

    flash_ptr += FLASH_PAGE_SIZE;
  }

  ctx->flash_deadline = make_timeout_time_ms(FLASH_DATA_WRITE_MS);
}
