extern crate imap;
extern crate serde;
extern crate serde_derive;
extern crate my3status;

use std::sync::mpsc;
use serde_derive::Deserialize;

#[derive(Deserialize)]
struct Account {
    host: String,
    port: u16,
    username: String,
    password: String,
}

#[derive(Deserialize)]
struct Config {
    accounts: Vec<Account>
}

enum Status {
    Error,
    Normal(usize)
}

type Sender = mpsc::Sender<(usize, Status)>;
type Receiver = mpsc::Receiver<(usize, Status)>;

type AccountStatusMap = std::collections::HashMap<usize, Status>;

#[no_mangle]
pub extern fn my3status_module_init(state_ptr: my3status::StatePtr) {
    let state = my3status::State::new(state_ptr);

    let mut output = String::with_capacity(32);
    let module = my3status::register_module(state, "imap\0", &output, true);

    let config = load_config().expect("failed to load imap config");
    let (tx, rx) = mpsc::channel();

    std::thread::spawn(move || output_thread(rx, module, &mut output));

    for (pos, account) in config.accounts.into_iter().enumerate() {
        let tx = tx.clone();
        std::thread::spawn(move || monitor_thread(pos, account, tx));
    }
}

fn load_config() -> Result<Config, Box<dyn std::error::Error>> {
    let content = std::fs::read_to_string("/home/tobias/imap.toml")?;
    toml::from_str(&content).map_err(|e| e.into())
}

fn output_thread(rx: Receiver, module: my3status::Module, output: &mut String) {
    let mut account_statuses = AccountStatusMap::new();

    loop {
        let (account_id, status) = rx.recv().unwrap();

        account_statuses.insert(account_id, status);
        let new_output = aggregate_account_statuses(&account_statuses);

        module.visible(new_output.is_some());
        if new_output.is_none() { continue }

        module.with_output_lock(|| {
            output.clear();
            output.push_str(&new_output.unwrap());
        });
    }
}

fn aggregate_account_statuses(a: &AccountStatusMap) -> Option<String> {
    let mut unseen_count = 0;
    let mut error_count = 0;

    for (_id, status) in a.iter() {
        match status {
            Status::Normal(0) => continue,
            Status::Normal(c) => unseen_count += c,
            Status::Error => error_count += 1,
        }
    }

    if unseen_count == 0 && error_count == 0 { return None }

    Some(match (unseen_count, error_count) {
        (0, _) => "📪 ⚠️\0".to_owned(),
        (_, 0) => format!("📬 {}\0", unseen_count),
        (_, _) => format!("📬⚠️ {}\0", unseen_count),
    })
}

fn monitor_thread(id: usize, account: Account, tx: Sender) {
    loop {
        let e = monitor_unseen_messages(id, &account, &tx).err().unwrap();

        tx.send((id, Status::Error)).unwrap();

        eprintln!("imap error: {}", e);
        eprintln!("reconnecting in 3 seconds...");

        std::thread::sleep(std::time::Duration::from_secs(3));
    }
}

fn monitor_unseen_messages(id: usize, account: &Account, tx: &Sender) -> Result<(), imap::Error> {
    let client = imap::ClientBuilder::new(&account.host, account.port)
        .native_tls()?;

    let mut session = client
        .login(&account.username, &account.password)
        .map_err(|e| e.0)?;

    session.select("INBOX")?;

    loop {
        let results = session.search("UNSEEN")?;

        let i = results.len();
        tx.send((id, Status::Normal(i))).unwrap();
        eprintln!("unseen messages: {}, idling...", i);

        let idle_handle = session.idle()?;
        idle_handle.wait_keepalive_while(|_| { false })?;
    }
}