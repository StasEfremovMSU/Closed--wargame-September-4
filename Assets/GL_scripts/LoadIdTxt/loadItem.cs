using System.Collections;
using System.Collections.Generic;
using UnityEngine;

// Класс для загрузки предметов вооружения

public class loadItem {

    private int temp;

    // Номер объекта
    public int[] number; 
    // Название
    public string[] name;

    // Класс предмета
    
    // Дает защиту
    public int[] def;
    // Атаку
    public int[] damage;
    // Мощь
    public int[] power;
    // Вес
    public float[] weight;

    public loadItem(int n)
    {
        temp = 0;
        number = new int[n];
        name = new string[n];
        def = new int[n];
        damage = new int[n];
        power = new int[n];
        weight = new float[n];

    }

    public void additem(string[] a)
    {
       //Debug.Log(" addItem ");

        number[temp] = int.Parse(a[0]);

        name[temp] = a[1];

        def[temp] = int.Parse(a[2]);

        damage[temp] = int.Parse(a[3]);
        
        power[temp] = int.Parse(a[4]);
        
        weight[temp] = int.Parse(a[5]);

        temp++;
    }

    public void putitem()
    {

    }


}
