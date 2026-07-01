const moduleInputs = document.querySelectorAll("[data-module]");
const moduleCount = document.querySelector("[data-module-count]");
const totalPrice = document.querySelector("[data-total]");
const buyBaseButton = document.querySelector("[data-buy-base]");
const walletBalance = document.querySelector("[data-wallet-balance]");
const topupButtons = document.querySelectorAll("[data-topup]");
const tokenMeter = document.querySelector("[data-token-meter]");
const tokenNote = document.querySelector("[data-token-note]");
const loginForm = document.querySelector("[data-login-form]");
const loginStatus = document.querySelector("[data-login-status]");

const basePrice = 15;
const modulePrice = 5;
let balance = 0;

function updateCart() {
  const selected = [...moduleInputs].filter((input) => input.checked);
  const total = basePrice + selected.length * modulePrice;

  moduleCount.textContent = selected.length;
  totalPrice.textContent = `€${total}`;
}

function updateWallet() {
  walletBalance.textContent = `€${balance}`;
  const meterWidth = Math.min(100, 18 + balance * 2);
  tokenMeter.style.width = `${meterWidth}%`;
  tokenNote.textContent = balance > 0
    ? `Доступно примерно €${balance} для оплаты AI API-токенов.`
    : "Пополните кошелек, чтобы оплачивать API-токены Claude, Gemini или других провайдеров.";
}

moduleInputs.forEach((input) => {
  input.addEventListener("change", updateCart);
});

buyBaseButton.addEventListener("click", () => {
  buyBaseButton.textContent = "Базовая версия выбрана";
  buyBaseButton.classList.add("is-confirmed");
});

topupButtons.forEach((button) => {
  button.addEventListener("click", () => {
    balance += Number(button.dataset.topup);
    updateWallet();
  });
});

loginForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const form = new FormData(loginForm);
  const email = form.get("email");
  loginStatus.textContent = `Выполнен вход как ${email}. Покупки и баланс готовы к синхронизации.`;
  loginForm.classList.add("is-logged-in");
});

updateCart();
updateWallet();
